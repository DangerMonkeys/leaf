/*
 * baro.cpp
 *
 */
#include "instruments/baro.h"

#include "instruments/vario_timing.h"

#include "diagnostics/fatal_error.h"
#include "diagnostics/flight_diagnostics.h"
#include "hardware/Leaf_I2C.h"
#include "hardware/ms5611.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "logging/telemetry.h"
#include "ui/audio/speaker.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "utils/flags_enum.h"
#include "utils/magic_enum.h"

constexpr size_t BARO_STARTUP_DISCARD_SAMPLES = 20;

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
  validAltF_ = false;
  validAltAdjusted_ = false;
  validAltAtLaunch_ = false;
  validAltInitial_ = false;
  validClimbRateRaw_ = false;
  validClimbRateFiltered_ = false;
  validClimbRate1SecAverage_ = false;
  climbFilter.reset();
  climb1SecFilter.reset();
  climbDisplayHistory_.fill(0);
  climbDisplayHistoryCount_ = 0;
  climbDisplayHistoryIndex_ = 0;
  climbDisplayBlockSum_ = 0;
  climbDisplayBlockCount_ = 0;
  startupDiscardSamplesRemaining_ = BARO_STARTUP_DISCARD_SAMPLES;

  state_ = State::WaitingForFirstReading;
}

void Barometer::on_receive(const PressureUpdate& msg) {
  if (uint32_t(millis() - msg.t) > vario_timing::SENSOR_FRESH_MS || msg.pressure <= 0) return;
  const uint32_t gap = uint32_t(msg.t - lastPressureTime_);
  if (state_ == State::Ready && int32_t(gap) > 0 && gap > FlightDiagnostics::GAP_MS)
    flightDiagnostics.record(FlightDiagnostics::Kind::PressureGap, msg.t, gap);
  lastPressureTime_ = msg.t;
  if (state_ == State::Uninitialized) {
    init();
  }

  if (state_ == State::WaitingForFirstReading) {
    if (startupDiscardSamplesRemaining_ > 0) {
      startupDiscardSamplesRemaining_--;
    } else {
      firstReading(msg);
    }
  } else if (state_ == State::Sleeping) {
    // Do nothing
  } else if (state_ == State::Ready) {
    setPressureAlt(msg.pressure);  // calculate Pressure Altitude adjusted for temperature
    filterAltitude();
  } else {
    fatalError("Barometer state %s (%u) in on_receive", nameOf(state_).c_str(), state_);
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
  fatalError("%s while %s (%u)", action, nameOf(actual).c_str(), actual);
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

  if (LOG::BARO && bus_) {
    String baroName = "baro mb*100,";
    String baroEntry = baroName + String(pressure_);
    bus_->receive(CommentMessage(baroEntry));
  }
}

int32_t Barometer::climbRateFiltered() {
  assertState("Barometer::climbRateFiltered", State::Ready);
  if (!validClimbRateFiltered_) {
    fatalError("Barometer::climbRateFiltered accessed before valid");
  }
  return climbRateFiltered_;
}

bool Barometer::climbRateFilteredValid() {
  if (!imu.velocityValid()) return false;
  if (state_ != State::Ready) return false;
  if (!validClimbRateFiltered_) return false;
  return true;
}

int32_t Barometer::climbRateForDisplay() {
  const int32_t current = climbRateFiltered();
  if (climbDisplayAverageSeconds_ == 0 || climbDisplayHistoryCount_ == 0) return current;

  const size_t requestedSamples = climbDisplayAverageSeconds_ * CLIMB_DISPLAY_SAMPLES_PER_SECOND;
  const size_t validHistoryCount =
      min<size_t>(climbDisplayHistoryCount_, CLIMB_DISPLAY_HISTORY_SIZE);
  const size_t historyWriteIndex = climbDisplayHistoryIndex_ % CLIMB_DISPLAY_HISTORY_SIZE;
  const size_t samplesToAverage = min(requestedSamples, validHistoryCount);
  int32_t sum = 0;
  for (size_t i = 0; i < samplesToAverage; ++i) {
    const size_t historyIndex =
        (historyWriteIndex + CLIMB_DISPLAY_HISTORY_SIZE - 1 - i) % CLIMB_DISPLAY_HISTORY_SIZE;
    sum += climbDisplayHistory_[historyIndex];
  }
  return sum / static_cast<int32_t>(samplesToAverage);
}

void Barometer::setClimbDisplayAverageSeconds(uint8_t seconds) {
  climbDisplayAverageSeconds_ =
      seconds > CLIMB_DISPLAY_AVERAGE_MAX_SECONDS ? CLIMB_DISPLAY_AVERAGE_MAX_SECONDS : seconds;
}

int32_t Barometer::climbRate1SecAverage() {
  assertState("Barometer::climbRate1SecAverage", State::Ready);
  if (!validClimbRate1SecAverage_) {
    fatalError("Barometer::climbRate1SecAverage accessed before valid");
  }
  return climbRate1SecAverage_;
}

bool Barometer::climbRate1SecAverageValid() {
  if (!imu.velocityValid()) return false;
  if (state_ != State::Ready) return false;
  if (!validClimbRate1SecAverage_) return false;
  return true;
}

uint16_t Barometer::startupSamplesCompleted() const {
  if (state_ == State::Uninitialized || state_ == State::Sleeping) return 0;
  if (state_ != State::WaitingForFirstReading) return BARO_STARTUP_DISCARD_SAMPLES;
  return BARO_STARTUP_DISCARD_SAMPLES - startupDiscardSamplesRemaining_;
}

uint16_t Barometer::startupSamplesRequired() const { return BARO_STARTUP_DISCARD_SAMPLES; }

void Barometer::filterAltitude() {
  // Filter Pressure and calculate Final Altitude Values
  // Note, IMU will have taken an accel reading and updated the Kalman

  // get instant climb rate
  if (!imu.velocityValid()) {
    invalidateClimb();
    return;
  }
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

void Barometer::invalidateClimb() {
  validClimbRateRaw_ = validClimbRateFiltered_ = validClimbRate1SecAverage_ = false;
  speaker.updateVarioNote(0);
  // Stale output can be suppressed without losing an already qualified filter's history.
  // Genuine resets still clear averaging and require normal startup qualification.
  if (imu.kalmanValid() && imu.startupSamplesCompleted() == imu.startupSamplesRequired()) return;
  climbFilter.reset();
  climb1SecFilter.reset();
  climbDisplayHistory_.fill(0);
  climbDisplayHistoryCount_ = climbDisplayHistoryIndex_ = climbDisplayBlockCount_ = 0;
  climbDisplayBlockSum_ = 0;
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
  updateClimbDisplayHistory();

  climb1SecFilter.update(climbRateRaw_);
  const float climb1SecAvg = climb1SecFilter.getAverage();
  if (isnan(climb1SecAvg) || isinf(climb1SecAvg)) {
    fatalError("climb1SecAvg in Barometer::filterClimb was %g after climb1SecFilter.getAverage()",
               climb1SecAvg);
  }
  climbRate1SecAverage_ = static_cast<int32_t>(climb1SecAvg * 100);
  validClimbRate1SecAverage_ = true;
}

void Barometer::updateClimbDisplayHistory() {
  constexpr uint8_t BARO_SAMPLES_PER_DISPLAY_SAMPLE =
      BARO_SAMPLES_PER_SECOND / CLIMB_DISPLAY_SAMPLES_PER_SECOND;

  climbDisplayBlockSum_ += climbRateFiltered_;
  climbDisplayBlockCount_++;
  if (climbDisplayBlockCount_ < BARO_SAMPLES_PER_DISPLAY_SAMPLE) return;

  const int32_t blockAverage = climbDisplayBlockSum_ / climbDisplayBlockCount_;
  // Normalize before dereferencing so even a corrupted retained index cannot become a wild write.
  if (climbDisplayHistoryIndex_ >= CLIMB_DISPLAY_HISTORY_SIZE) climbDisplayHistoryIndex_ = 0;
  climbDisplayHistory_[climbDisplayHistoryIndex_] =
      static_cast<int16_t>(constrain(blockAverage, -32768L, 32767L));
  climbDisplayHistoryIndex_ = (climbDisplayHistoryIndex_ + 1) % CLIMB_DISPLAY_HISTORY_SIZE;
  if (climbDisplayHistoryCount_ < CLIMB_DISPLAY_HISTORY_SIZE) climbDisplayHistoryCount_++;
  climbDisplayBlockSum_ = 0;
  climbDisplayBlockCount_ = 0;
}

// ^^^ Device reading & data processing ^^^
