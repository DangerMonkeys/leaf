/*
 * baro.cpp
 *
 */
#include "instruments/baro.h"

#include "hardware/Leaf_I2C.h"
#include "hardware/ms5611.h"
#include "hardware/temp_rh.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "logging/telemetry.h"
#include "storage/sd_card.h"
#include "ui/audio/speaker.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "utils/flags_enum.h"

#define DEBUG_BARO 0  // flag for printing serial debugging messages

// number of seconds for average climb rate (this is used for smoother data in places like
// glide ratio, where rapidly fluctuating glide numbers aren't as useful as a several-second
// average)
#define CLIMB_AVERAGE 4

// Singleton sensor to use in barometer
MS5611 sensor;
// Singleton barometer instance for device
Barometer baro(&sensor);

void Barometer::adjustAltSetting(int8_t dir, uint8_t count) {
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
}

// solve for the altimeter setting required to make corrected-pressure-altitude match gps-altitude
bool Barometer::syncToGPSAlt() {
  bool success = false;
  if (gps.altitude.isValid()) {
    altimeterSetting =
        pressure / (3386.389 * pow(1 - gps.altitude.meters() * 100 / 4433100.0, 1 / 0.190264));
    settings.vario_altSetting = altimeterSetting;
    calculateAlts();  // recalculate altitudes with new adjusted pressure setting
    success = true;
  }
  return success;
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
  if (settings.vario_altSetting > 28.0 && settings.vario_altSetting < 32.0)
    altimeterSetting = settings.vario_altSetting;
  else
    altimeterSetting = 29.921;

  pressureSource_->init();

  // after initialization, get first baro sensor reading to populate values
  getFirstReading();

  pressureSource_->startMeasurement();
  task_ = BarometerTask::Measure;
}

void Barometer::getFirstReading(void) {
  pressureSource_->startMeasurement();
  PressureUpdateResult result = pressureSource_->update();
  while (!FLAG_SET(result, PressureUpdateResult::PressureReady)) {
    delay(10);
    result = pressureSource_->update();
  }
  pressure = pressureSource_->getPressure();

  /* TODO: remove pressure filter; we'll rely on Kalman filter for smoothing */
  /*
  // load the filters with our current start-up pressure (and climb is assumed to be 0)
  for (int i = 1; i <= FILTER_VALS_MAX; i++) {
    pressureFilterVals_[i] = pressure_;
    climbFilterVals_[i] = 0;
  }
  // and set bookmark index to 1
  pressureFilterVals_[0] = 1;
  climbFilterVals_[0] = 1;

  // and save starting filtered output to current pressure
  pressureFiltered = pressure_;
  pressureRegression_ = pressure_;

  // and start off the linear regression version
  // pressure_lr.update((double)millis(), (double)pressure_);
  */

  calculatePressureAlt();  // calculate altitudes

  // initialize all the other alt variables with current altitude to start

  // used to calculate the alt change for climb rate.  Assume we're stationary
  // to start (previous Alt = Current ALt, so climb rate is zero).  Note: Climb
  // rate uses the un-adjusted (standard) altitude
  lastAlt_ = alt;
  // also save first value to use as starting point (we assume the
  // saved altimeter setting is correct for now, so use adjusted)
  altInitial = alt;
  // save the starting value as launch altitude (Launch will
  // be updated when timer starts)
  altAtLaunch = altAdjusted;
}

void Barometer::resetLaunchAlt() { altAtLaunch = altAdjusted; }

void Barometer::setFilterSamples(size_t nSamples) {
  pressureFilter.setSampleCount(nSamples);
  climbFilter.setSampleCount(nSamples);
}

void Barometer::sleep() { sleeping_ = true; }
void Barometer::wake() {
  sleeping_ = false;
  getFirstReading();  // after waking, get first baro sensor reading to populate values
}

void Barometer::startMeasurement() { pressureSource_->startMeasurement(); }

void Barometer::update() {
  // (we don't need to update temp as frequently so we choose to skip it if desired)
  // the baro senor requires ~9ms between the command to prep the ADC and actually reading the
  // value. Since this delay is required between both pressure and temp values, we break the sensor
  // processing up into several steps, to allow other code to process while we're waiting for the
  // ADC to become ready.

  // only do baro updates if we're not "sleeping" (i.e. in PowerOff state)
  if (sleeping_) {
    // set climb to 0 so we don't have any vario beeps etc
    climbRate = 0;
    climbRateAverage = 0;
    climbRateFiltered = 0;
    speaker_updateVarioNote(climbRateFiltered);
    firstClimbInitialization_ = true;  //  reset so we don't get false climb on wake-up
    return;
  }

  if (DEBUG_BARO) {
    Serial.print("baro task: ");
    Serial.print((int)task_);
    Serial.print(" time: ");
    Serial.println(micros());
  }

  if (task_ == BarometerTask::None) {
    // Do nothing
  } else if (task_ == BarometerTask::Measure) {
    PressureUpdateResult sensorResult = pressureSource_->update();

    if (FLAG_SET(sensorResult, PressureUpdateResult::PressureReady)) {
      // (even if we skipped some steps above because of mis-reads or mis-timing, we can still
      // calculate a "new" corrected pressure value based on the old ADC values.  It will be a
      // repeat value, but it keeps the filter buffer moving on time)
      calculatePressureAlt();  // calculate Pressure Altitude adjusted for temperature
      task_ = BarometerTask::FilterAltitude;
    }
  } else if (task_ == BarometerTask::FilterAltitude) {
    // Filter Pressure and calculate Final Altitude Values
    // Note, IMU will have taken an accel reading and updated the Kalman
    // Filter after Baro_step_2 but before Baro_step_3

    // get instant climb rate
    climbRate = (float)kalmanvert.getVelocity();  // in m/s

    // TODO: get altitude from Kalman Filter when Baro/IMU/'vario' are restructured
    // alt = int32_t(kalmanvert.getPosition() * 100);  // in cm above sea level

    // filter ClimbRate
    filterClimb();

    // finally, update the speaker sound based on the new climbrate
    speaker_updateVarioNote(climbRateFiltered);

    if (DEBUG_BARO) Serial.println("**BR** climbRate Filtered: " + String(climbRateFiltered));

    pressureSource_->startMeasurement();
    task_ = BarometerTask::Measure;
  } else {
    // TODO: Write generic fatal error handler that prints error message to screen before stopping
    Serial.printf("Fatal error: Barometer was conducting unknown task %d\n", (int)task_);
    while (true);
  }
}

// ^^^ Device Management ^^^

// vvv Device reading & data processing vvv

void Barometer::calculatePressureAlt() {
  pressure = pressureSource_->getPressure();

  // record datapoint on SD card if datalogging is turned on

  String baroName = "baro mb*100,";
  String baroEntry = baroName + String(pressure);
  Telemetry.writeText(baroEntry);

  // calculate all altitudes (standard, adjusted, and above launch)
  calculateAlts();
}

// Filter Pressure Values
//
// To reduce noise, we perform a moving average of the last N data points.  We create an array of
// datapoints pressureFilterVals[] with size FILTER_VALS_MAX + 1,
//     saving one spot [0] for an index/bookmark to where the next value should be stored.
// The value of the bookmark rises from 1 up to FILTER_VALS_MAX, then wraps around to 1 again,
// overwriting old data. For proper behavior on startup, the each element of the array is loaded
// with the current (first) pressure reading.
//
// When filtering the pressure values, the most recent N values are summed up (from the bookmarked
// spot back to N spots before that), then divided by N to get the average.
//  (where N is calculated based on sensitivity user preference vario_sensitivity)
//
// NOTE: We have also explored performing a linear regeression on the last N samples to get a more
// accurate filtered value.  This is still in testing, and not currently being used.
//
void Barometer::filterPressure(void) {
  // new way with regression:
  pressureLR_.update((double)millis(), (double)alt);
  LinearFit fit = pressureLR_.fit();
  pressureRegression_ = linear_value(&fit, (double)millis());

  // old way with averaging last N values equally:
  pressureFilter.update(pressure);
  pressureFiltered = Pressure::fromMillibars(pressureFilter.getAverage());
}

void Barometer::calculateAlts() {
  // float altitude in meters with standard altimeter setting
  altF = 44331.0 * (1.0 - pow((float)pressure / 101325.0, (.190264)));

  // int altitude in cm with standard altimeter setting
  alt = int32_t(altF * 100);

  // int altitude in cm with adjusted altimeter setting
  altAdjusted = 4433100.0 * (1.0 - pow((float)pressure / (altimeterSetting * 3386.389), (.190264)));
  altAboveLaunch = altAdjusted - altAtLaunch;
}

// Filter ClimbRate
void Barometer::filterClimb() {
  // lastAlt isn't set yet on boot-up, so just assume a zero climb rate for the first sample.
  if (firstClimbInitialization_) {
    climbRate = 0;
    climbRateFiltered = 0;
    climbRateAverage = 0;
    firstClimbInitialization_ = false;
    return;
  }

  // filter climb rate
  climbFilter.update(climbRate);

  // convert m/s -> cm/s to get the average climb rate
  climbRateFiltered = (int32_t)(climbFilter.getAverage() * 100);

  // now calculate the longer-running average climb value
  // (this is a smoother, slower-changing value for things like glide ratio, etc)
  int32_t total_samples = CLIMB_AVERAGE * 20;  // CLIMB_AVERAGE seconds * 20 samples/sec

  // use new value in the long-running average
  climbRateAverage = (climbRateAverage * (total_samples - 1) + climbRateFiltered) / total_samples;
}

// ^^^ Device reading & data processing ^^^
