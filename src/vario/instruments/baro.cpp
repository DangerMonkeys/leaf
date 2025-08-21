/*
 * baro.cpp
 *
 */
#include "instruments/baro.h"

#include "diagnostics/fatal_error.h"
#include "hardware/Leaf_I2C.h"
#include "hardware/ms5611.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "logging/telemetry.h"
#include "storage/sd_card.h"
#include "ui/audio/speaker.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "utils/flags_enum.h"
#include "utils/magic_enum.h"

// number of seconds for average climb rate (this is used for smoother data in places like
// glide ratio, where rapidly fluctuating glide numbers aren't as useful as a several-second
// average)
constexpr uint32_t CLIMB_AVERAGE_S = 4;

// number of seconds to average the climb rate before declaring that the averaged value is valid
constexpr uint32_t CLIMB_AVERAGE_INIT_S = 1;

// sample rate of altitudes; used to compute smaple count from duration
constexpr uint32_t SAMPLES_PER_SECOND = 20;

// Singleton barometer instance for device
Barometer baro;

void Barometer::adjustAltSetting(int8_t dir, uint8_t count) {
  // TODO(#192): check whether settings is initialized before reading/using
  float increase = .001;  //
  if (count >= 1) increase *= 10;
  if (count >= 8) increase *= 5;

  if (dir >= 1) {
    altimeterSetting += increase;
    if (altimeterSetting > 32.0) altimeterSetting = 32.0;
  } else if (dir <= -1) {
    altimeterSetting -= increase;
    if (altimeterSetting < 28.0) altimeterSetting = 28.0;
  }
  settings.vario_altSetting = altimeterSetting;

  // Invalidate adjusted altitudes
  validAltAdjusted_ = false;
}

bool Barometer::syncToGPSAlt() {
  if (state_ != State::Ready) return false;
  if (!gps.altitude.isValid()) return false;
  altimeterSetting =
      pressure_ / (3386.389 * pow(1 - gps.altitude.meters() * 100 / 4433100.0, 1 / 0.190264));
  // TODO(#192): check whether settings is initialized before reading/using
  settings.vario_altSetting = altimeterSetting;

  // Invalidate adjusted altitudes
  validAltAdjusted_ = false;

  return true;
}

// Conversion functions to change units
int32_t baro_altToUnits(int32_t alt_input, bool units_feet) {
  if (units_feet)
    alt_input = alt_input * 100 / 3048;  // convert cm to ft
  else
    alt_input /= 100;  // convert cm to m

  return alt_input;
}

float baro_climbToUnits(int32_t climbrate, bool units_fpm) {
  float climbrate_converted;
  if (units_fpm) {
    climbrate_converted =
        (int32_t)(climbrate * 197 / 1000 * 10);  // convert from cm/s to fpm (lose one significant
                                                 // digit and all decimal places)
  } else {
    climbrate = (climbrate + 5) /
                10;  // lose one decimal place and round off in the process (cm->decimeters)
    climbrate_converted =
        (float)climbrate / 10;  // Lose the other decimal place (decimeters->meters) and convert to
                                // float for ease of printing with the decimal in place
  }
  return climbrate_converted;
}

// vvv Device Management vvv

void Barometer::init(void) {
  // recover saved altimeter setting
  // TODO(#192): check whether settings is initialized before reading/using
  if (settings.vario_altSetting > 28.0 && settings.vario_altSetting < 32.0) {
    altimeterSetting = settings.vario_altSetting;
  } else {
    altimeterSetting = 29.921;
  }

  // Clear any state
  validClimbRateRaw_ = false;
  validClimbRateFiltered_ = false;
  climbFilter.reset();
  climbRateAverage_ = 0;
  nInitSamplesRemaining_ = CLIMB_AVERAGE_INIT_S * SAMPLES_PER_SECOND;

  state_ = State::WaitingForFirstReading;
}

void Barometer::on_receive(const PressureUpdate& msg) {
  if (state_ == State::Uninitialized) {
    init();
  }

  if (state_ == State::WaitingForFirstReading) {
    firstReading(msg);
  } else if (state_ == State::Sleeping) {
    // Do nothing
  } else if (state_ == State::Ready) {
    setPressureAlt(msg.pressure);  // calculate Pressure Altitude adjusted for temperature
    filterAltitude();
  } else {
    fatalError("Barometer state %s in on_receive", nameOf(state_));
  }
}

void Barometer::firstReading(const PressureUpdate& msg) {
  state_ = State::Ready;
  setPressureAlt(msg.pressure);
  setAltInitial();
  setLaunchAlt();
}

void Barometer::setLaunchAlt() {
  assertState("Barometer::setLaunchAlt", State::Ready);
  altAtLaunch_ = altAdjusted();
  validAltAtLaunch_ = true;
}

int32_t Barometer::altAtLaunch() {
  if (!validAltAtLaunch_) {
    fatalError("Barometer::altAtLaunch when launch altitude was not set");
    return 0;
  }
  return altAtLaunch_;
}

void Barometer::setAltInitial() {
  assertState("Barometer::setAltInitial", State::Ready);
  altInitial_ = alt();
  validAltInitial_ = true;
}

int32_t Barometer::altAboveInitial() {
  if (!validAltInitial_) {
    fatalError("Barometer::altAboveInitial when initial altitude was not set");
    return 0;
  }
  return alt() - altInitial_;
}

void Barometer::setFilterSamples(size_t nSamples) { climbFilter.setSampleCount(nSamples); }

void Barometer::sleep() {
  // TODO: only put Barometer to sleep once and remove Sleeping as a valid state to tell Barometer
  // to go to sleep from
  // TODO: do not put Barometer to sleep while uninitialized and remove Uninitialized as a valid
  // state to tell Barometer to go to sleep from
  assertState("Barometer::sleep", State::Uninitialized, State::WaitingForFirstReading, State::Ready,
              State::Sleeping);
  init();

  speaker.updateVarioNote(0);
  state_ = State::Sleeping;
}

void Barometer::wake() {
  assertState("Barometer::wake", State::Sleeping);
  state_ = State::WaitingForFirstReading;
}

// ^^^ Device Management ^^^

void Barometer::onUnexpectedState(const char* action, State actual) const {
  fatalError("%s while %s", action, nameOf(actual));
}

// vvv Device reading & data processing vvv

Pressure Barometer::pressure() const {
  assertState("Barometer::pressure", State::Ready);
  return pressure_;
}

float Barometer::altF() {
  assertState("Barometer::altF", State::Ready);
  if (!validAltF_) {
    // float altitude in meters with standard altimeter setting
    altF_ = 44331.0 * (1.0 - pow((float)pressure_ / 101325.0, (.190264)));
    if (isnan(altF_) || isinf(altF_)) {
      fatalError("altF was %g after calculating from pressure", altF_);
    }
    validAltF_ = true;
  }
  return altF_;
}

int32_t Barometer::alt() { return int32_t(altF() * 100); }

int32_t Barometer::altAdjusted() {
  assertState("Barometer::altAdjusted", State::Ready);
  if (!validAltAdjusted_) {
    validAltAdjusted_ = true;
    altAdjusted_ =
        4433100.0 * (1.0 - pow((float)pressure_ / (altimeterSetting * 3386.389), (.190264)));
  }
  return altAdjusted_;
}

void Barometer::setPressureAlt(int32_t newPressure) {
  pressure_ = newPressure;
  validAltF_ = false;
  validAltAdjusted_ = false;

  // record datapoint on SD card if datalogging is turned on
  String baroName = "baro mb*100,";
  String baroEntry = baroName + String(pressure_);
  Telemetry.writeText(baroEntry);
}

float Barometer::climbRate() {
  if (!validClimbRateRaw_) {
    fatalError("Barometer::climbRate accessed before valid");
  }
  return climbRateRaw_;
}

int32_t Barometer::climbRateFiltered() {
  assertState("Barometer::climbRateFiltered", State::Ready);
  if (!validClimbRateFiltered_) {
    fatalError("Barometer::climbRateFiltered accessed before valid");
  }
  return climbRateFiltered_;
}

float Barometer::climbRateAverage() {
  assertState("Barometer::climbRateAverage", State::Ready);
  if (nInitSamplesRemaining_ > 0) {
    fatalError(
        "Barometer::climbRateAverage accessed before initialized; waiting for %d samples more",
        nInitSamplesRemaining_);
  }
  return climbRateAverage_;
}

bool Barometer::climbRateAverageValid() { return nInitSamplesRemaining_ == 0; }

void Barometer::filterAltitude() {
  // Filter Pressure and calculate Final Altitude Values
  // Note, IMU will have taken an accel reading and updated the Kalman

  // get instant climb rate
  climbRateRaw_ = imu.getVelocity();  // in m/s
  if (isnan(climbRateRaw_) || isinf(climbRateRaw_)) {
    fatalError("climbRate in Barometer::filterAltitude was %g after imu.getVelocity()",
               climbRateRaw_);
  }
  validClimbRateRaw_ = true;

  // TODO: get altitude from Kalman Filter when Baro/IMU/'vario' are restructured
  // alt = int32_t(kalmanvert.getPosition() * 100);  // in cm above sea level

  // filter ClimbRate
  filterClimb();

  // finally, update the speaker sound based on the new climbrate
  if (validClimbRateFiltered_) {
    speaker.updateVarioNote(climbRateFiltered_);
  }
}

// Filter ClimbRate
void Barometer::filterClimb() {
  if (!validClimbRateRaw_) return;

  // filter climb rate
  if (isnan(climbRateRaw_) || isinf(climbRateRaw_)) {
    fatalError("climbRateRaw_ in Barometer::filterClimb was %g before climbFilter.update",
               climbRateRaw_);
  }
  climbFilter.update(climbRateRaw_);

  // convert m/s -> cm/s to get the average climb rate
  float climbFilterAvg = climbFilter.getAverage();
  if (isnan(climbFilterAvg) || isinf(climbFilterAvg)) {
    fatalError("climbRateAvg in Barometer::filterClimb was %g after climbFilter.getAverage()",
               climbFilterAvg);
  }
  climbRateFiltered_ = (int32_t)(climbFilterAvg * 100);
  validClimbRateFiltered_ = true;

  // use new value in the long-running average
  if (nInitSamplesRemaining_ > 1) {
    climbRateAverage_ += climbRateFiltered_;
    nInitSamplesRemaining_--;
  } else if (nInitSamplesRemaining_ == 1) {
    climbRateAverage_ =
        (climbRateAverage_ + climbRateFiltered_) / (CLIMB_AVERAGE_INIT_S * SAMPLES_PER_SECOND);
    nInitSamplesRemaining_ = 0;
  } else {
    // now calculate the longer-running average climb value
    // (this is a smoother, slower-changing value for things like glide ratio, etc)
    uint32_t total_samples = CLIMB_AVERAGE_S * SAMPLES_PER_SECOND;

    climbRateAverage_ =
        (climbRateAverage_ * (total_samples - 1) + climbRateFiltered_) / total_samples;
    if (isnan(climbRateAverage_) || isinf(climbRateAverage_)) {
      fatalError(
          "climbRateAverage in Barometer::filterClimb was %g after incorporating climbRateFiltered",
          climbRateAverage_);
    }
  }
}

// ^^^ Device reading & data processing ^^^
