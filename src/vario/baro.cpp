/*
 * baro.cpp
 *
 */
#include "baro.h"

#include "IMU.h"
#include "Leaf_I2C.h"
#include "SDcard.h"
#include "buttons.h"
#include "log.h"
#include "settings.h"
#include "speaker.h"
#include "telemetry.h"
#include "tempRH.h"

#define DEBUG_BARO 0  // flag for printing serial debugging messages

#define CLIMB_AVERAGE \
  4  // number of seconds for average climb rate (this is used for smoother data in places like
     // glide ratio, where rapidly fluctuating glide numbers aren't as useful as a several-second
     // average)

// Singleton barometer instance for device
Barometer baro;

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
  ALT_SETTING = altimeterSetting;
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

// vvv I2C Communication Functions vvv

uint8_t baro_sendCommand(uint8_t command) {
  Wire.beginTransmission(ADDR_BARO);
  Wire.write(command);
  uint8_t result = Wire.endTransmission();
  // if (DEBUG_BARO) { Serial.print("Baro Send Command Result: "); Serial.println(result); }
  return result;
}

uint16_t baro_readCalibration(uint8_t PROMaddress) {
  uint16_t value = 0;  // This will be the final 16-bit output from the ADC
  uint8_t command =
      0b10100000;  // The command to read from the specified address is 1 0 1 0 ad2 ad1 ad0 0
  command += (PROMaddress << 1);  // Add PROM address bits to the command byte

  baro_sendCommand(command);
  Wire.requestFrom(ADDR_BARO, 2);
  value += (Wire.read() << 8);
  value += (Wire.read());

  return value;
}

uint32_t baro_readADC() {
  uint32_t value = 0;  // This will be the final 24-bit output from the ADC
  // if (DEBUG_BARO) { Serial.println("Baro sending Read ADC command"); }
  baro_sendCommand(0b00000000);
  Wire.requestFrom(ADDR_BARO, 3);
  value += (Wire.read() << 16);
  // if (DEBUG_BARO) { Serial.print("Baro ADC Value 16: "); Serial.println(value); }
  value += (Wire.read() << 8);
  // if (DEBUG_BARO) { Serial.print("Baro ADC Value 8: "); Serial.println(value); }
  value += (Wire.read());
  // if (DEBUG_BARO) { Serial.print("Baro ADC Value 0: "); Serial.println(value); }

  return value;
}
// ^^^ I2C Communication Functions ^^^

// vvv Device Management vvv

void Barometer::init(void) {
  // recover saved altimeter setting
  if (ALT_SETTING > 28.0 && ALT_SETTING < 32.0)
    altimeterSetting = ALT_SETTING;
  else
    altimeterSetting = 29.92;

  // reset baro sensor for initialization
  reset();
  delay(2);

  // read calibration values
  C_SENS_ = baro_readCalibration(1);
  C_OFF_ = baro_readCalibration(2);
  C_TCS_ = baro_readCalibration(3);
  C_TCO_ = baro_readCalibration(4);
  C_TREF_ = baro_readCalibration(5);
  C_TEMPSENS_ = baro_readCalibration(6);

  // after initialization, get first baro sensor reading to populate values
  delay(10);        // wait for baro sensor to be ready
  update(1, true);  // send convert-pressure command
  delay(10);        // wait for baro sensor to process
  update(0, true);  // read pressure, send convert-temp command
  delay(10);        // wait for baro sensor to process
  update(0, true);  // read temp, and calculate adjusted pressure
  delay(10);

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
  // pressure_lr.update((double)millis(), (double)pressure);

  update(0, true);  // calculate altitudes

  // initialize all the other alt variables with current altitude to start
  lastAlt_ = alt;    // used to calculate the alt change for climb rate.  Assume we're stationary
                     // to start (previous Alt = Current ALt, so climb rate is zero).  Note: Climb
                     // rate uses the un-adjusted (standard) altitude
  altInitial = alt;  // also save first value to use as starting point (we assume the
                     // saved altimeter setting is correct for now, so use adjusted)
  altAtLaunch = altAdjusted;  // save the starting value as launch altitude (Launch will
                              // be updated when timer starts)

  if (DEBUG_BARO) {
    Serial.println("Baro initialization values:");
    Serial.print("  C_SENS:");
    Serial.println(C_SENS_);
    Serial.print("  C_OFF:");
    Serial.println(C_OFF_);
    Serial.print("  C_TCS:");
    Serial.println(C_TCS_);
    Serial.print("  C_TCO:");
    Serial.println(C_TCO_);
    Serial.print("  C_TREF:");
    Serial.println(C_TREF_);
    Serial.print("  C_TEMPSENS:");
    Serial.println(C_TEMPSENS_);
    Serial.print("  D1:");
    Serial.println(D1_P_);
    Serial.print("  D2:");
    Serial.println(D2_T_);
    Serial.print("  dT:");
    Serial.println(dT_);
    Serial.print("  TEMP:");
    Serial.println(temp_);
    Serial.print("  OFF1:");
    Serial.println(OFF1_);
    Serial.print("  SENS1:");
    Serial.println(SENS1_);
    Serial.print("  P_ALT:");
    Serial.println(alt);

    Serial.println(" ");
  }
}

void Barometer::reset(void) {
  unsigned char command = 0b00011110;  // This is the command to reset, and for the sensor to copy
                                       // calibration data into the register as needed
  baro_sendCommand(command);
  delay(3);  // delay time required before sensor is ready
}

void Barometer::resetLaunchAlt() { altAtLaunch = altAdjusted; }

void Barometer::wake() { sleeping_ = false; }
void Barometer::sleep() { sleeping_ = true; }

void Barometer::update(bool startNewCycle, bool doTemp) {
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
    return;
  }

  // First check if ADC is not busy (i.e., it's been at least 9ms since we sent a "convert ADC"
  // command)
  unsigned long microsNow = micros();
  if (microsNow - baroADCStartTime_ > 9000) {
    baroADCBusy_ = false;
  } else {
    Serial.print("BARO BUSY!  Executing Process Step # ");
    Serial.print(processStep_);
    Serial.print("  Micros since last: ");
    Serial.println(microsNow - baroADCStartTime_);
  }

  if (startNewCycle) processStep_ = 0;

  if (DEBUG_BARO) {
    Serial.print("baro step: ");
    Serial.print(processStep_);
    Serial.print(" NewCycle? ");
    Serial.print(startNewCycle);
    Serial.print(" time: ");
    Serial.println(micros());
  }

  switch (processStep_) {
    case 0:  // SEND CONVERT PRESSURE COMMAND
      if (!baroADCBusy_) {
        baroADCStartTime_ = micros();
        baro_sendCommand(CMD_CONVERT_PRESSURE);  // Prep baro sensor ADC to read raw pressure value
                                                 // (then come back for step 2 in ~10ms)
        baroADCBusy_ = true;      // ADC will be busy now since we sent a conversion command
        baroADCPressure_ = true;  // We will have a Pressure value in the ADC when ready
        baroADCTemp_ =
            false;  // We won't have a Temp value (even if the ADC was holding an unread Temperature
                    // value, we're clearning that out since we sent a Pressure command)
      }
      break;

    case 1:  // READ PRESSURE THEN SEND CONVERT TEMP COMMAND
      if (!baroADCBusy_ && baroADCPressure_) {
        D1_P_ = baro_readADC();  // Read raw pressure value
        baroADCPressure_ = false;
        // baroTimeStampPressure = micros() - baroTimeStampPressure; // capture duration between
        // prep and read
        if (D1_P_ == 0)
          D1_P_ = D1_Plast_;  // use the last value if we get an invalid read
        else
          D1_Plast_ = D1_P_;  // otherwise save this value for next time if needed
        // baroTimeStampTemp = micros();

        if (doTemp) {
          baroADCStartTime_ = micros();
          baro_sendCommand(CMD_CONVERT_TEMP);  // Prep baro sensor ADC to read raw temperature value
                                               // (then come back for step 3 in ~10ms)
          baroADCBusy_ = true;
          baroADCTemp_ = true;       // We will have a Temperature value in the ADC when ready
          baroADCPressure_ = false;  // We won't have a Pressure value (even if the ADC was holding
                                     // an unread Pressure value, we're clearning that out since we
                                     // sent a Temperature command)
        }
      }
      break;

    case 2:  // READ TEMP THEN CALCULATE ALTITUDE
      if (doTemp) {
        if (!baroADCBusy_ && baroADCTemp_) {
          D2_T_ = baro_readADC();  // read digital temp data
          baroADCTemp_ = false;
          // baroTimeStampTemp = micros() - baroTimeStampTemp; // capture duration between prep and
          // read
          if (D2_T_ == 0)
            D2_T_ = D2_Tlast_;  // use the last value if we get a misread
          else
            D2_Tlast_ = D2_T_;  // otherwise save this value for next time if needed
        }
      }
      // (even if we skipped some steps above because of mis-reads or mis-timing, we can still
      // calculate a "new" corrected pressure value based on the old ADC values.  It will be a
      // repeat value, but it keeps the filter buffer moving on time)
      calculatePressure();  // calculate Pressure adjusted for temperature
      break;

    case 3:  // Filter Pressure and calculate Final Altitude Values
      // filterPressure();
      // calculateAlt();  // filter pressure alt value
      // updateClimb();   // update and filter climb rate
      // if (DEBUG_BARO) debugPrint();

      climbRateFiltered = int32_t(kalmanvert.getVelocity() * 100);
      alt = int32_t(kalmanvert.getPosition() * 100);

      int32_t total_samples = CLIMB_AVERAGE * FILTER_VALS_MAX;

      climbRateAverage =
          (climbRateAverage * (total_samples - 1) + climbRateFiltered) / total_samples;

      // finally, update the speaker sound based on the new climbrate
      speaker_updateVarioNote(climbRateFiltered);

      Serial.println("**BR** climbRate Filtered: " + String(climbRateFiltered));

      break;
  }
  processStep_++;
}

// ^^^ Device Management ^^^

// vvv Device reading & data processing vvv

void Barometer::calculatePressure() {
  // calculate temperature (in 100ths of degrees C, from -4000 to 8500)
  dT_ = D2_T_ - ((int32_t)C_TREF_) * 256;
  int32_t TEMP = 2000 + (((int64_t)dT_) * ((int64_t)C_TEMPSENS_)) / pow(2, 23);

  // calculate sensor offsets to use in pressure & altitude calcs
  OFF1_ = (int64_t)C_OFF_ * pow(2, 16) + (((int64_t)C_TCO_) * dT_) / pow(2, 7);
  SENS1_ = (int64_t)C_SENS_ * pow(2, 15) + ((int64_t)C_TCS_ * dT_) / pow(2, 8);

  // low temperature compensation adjustments
  TEMP2_ = 0;
  OFF2_ = 0;
  SENS2_ = 0;
  if (TEMP < 2000) {
    TEMP2_ = pow((int64_t)dT_, 2) / pow(2, 31);
    OFF2_ = 5 * pow((TEMP - 2000), 2) / 2;
    SENS2_ = 5 * pow((TEMP - 2000), 2) / 4;
  }
  // very low temperature compensation adjustments
  if (TEMP < -1500) {
    OFF2_ = OFF2_ + 7 * pow((TEMP + 1500), 2);
    SENS2_ = SENS2_ + 11 * pow((TEMP + 1500), 2) / 2;
  }
  TEMP = TEMP - TEMP2_;
  OFF1_ = OFF1_ - OFF2_;
  SENS1_ = SENS1_ - SENS2_;

  // Filter Temp if necessary due to noise in values
  temp_ = TEMP;  // TODO: actually filter if needed

  // calculate temperature compensated pressure (in 100ths of mbars)
  pressure_ = ((uint64_t)D1_P_ * SENS1_ / (int64_t)pow(2, 21) - OFF1_) / pow(2, 15);

  // record datapoint on SD card if datalogging is turned on

  String baroName = "baro mb*100,";
  String baroEntry = baroName + String(pressure_);
  Telemetry.writeText(baroEntry);

  // calculate instant altitude (for kalman filter)
  altF = 44331.0 * (1.0 - pow((float)pressure_ / 101325.0, (.190264)));
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
//  (where N is calculated based on sensitivity user preference VARIO_SENSE)
//
// NOTE: We have also explored performing a linear regeression on the last N samples to get a more
// accurate filtered value.  This is still in testing, and not currently being used.
//
void Barometer::filterPressure(void) {
  // first calculate filter size based on user preference
  switch (VARIO_SENSE) {
    case 1:
      filterValsPref_ = 30;
      break;
    case 2:
      filterValsPref_ = 25;
      break;
    case 3:
      filterValsPref_ = 20;
      break;
    case 4:
      filterValsPref_ = 15;
      break;
    case 5:
      filterValsPref_ = 10;
      break;
    default:
      filterValsPref_ = 20;
      break;
  }

  // new way with regression:
  pressureLR_.update((double)millis(), (double)alt);
  LinearFit fit = pressureLR_.fit();
  pressureRegression_ = linear_value(&fit, (double)millis());

  // old way with averaging last N values equally:
  pressureFiltered = 0;
  int8_t filterBookmark = pressureFilterVals_[0];  // start at the saved spot in the filter array
  int8_t filterIndex =
      filterBookmark;  // and create an index to track all the values we need for averaging

  pressureFilterVals_[filterBookmark] = pressure_;  // load in the new value at the bookmarked spot
  if (++filterBookmark > FILTER_VALS_MAX)           // increment bookmark for next time
    filterBookmark = 1;                             // wrap around the array for next time if needed
  pressureFilterVals_[0] = filterBookmark;          // and save the bookmark for next time

  // sum up all the values from this spot and previous, for the correct number of samples (user
  // pref)
  for (int i = 0; i < filterValsPref_; i++) {
    pressureFiltered += pressureFilterVals_[filterIndex];
    filterIndex--;
    if (filterIndex <= 0) filterIndex = FILTER_VALS_MAX;  // wrap around the array
  }
  pressureFiltered /= filterValsPref_;  // divide to get the average
}

void Barometer::calculateAlt() {
  // calculate altitude in cm
  alt = 4433100.0 * (1.0 - pow((float)pressureFiltered / 101325.0,
                               (.190264)));  // standard altimeter setting

  altAdjusted = 4433100.0 * (1.0 - pow((float)pressureFiltered / (altimeterSetting * 3386.389),
                                       (.190264)));  // adjustable altimeter setting
  altAboveLaunch = altAdjusted - altAtLaunch;
}

// Update Climb
void Barometer::updateClimb() {
  // lastAlt isn't set yet on boot-up, so just assume a zero climb rate for the first sample.
  if (firstClimbInitialization_) {
    climbRate = 0;
    climbRateFiltered = 0;
    climbRateAverage = 0;
    firstClimbInitialization_ = false;
    return;
  }

  // TODO: incorporate ACCEL for added precision/accuracy

  // calculate climb rate based on standard altimeter setting (this way climb doesn't artificially
  // change if the setting is adjusted)
  climbRate =
      (alt - lastAlt_) *
      20;  // climb is updated every 1/20 second, so climb rate is cm change per 1/20sec * 20
  lastAlt_ = alt;  // store last alt value for next time

  // filter climb rate
  int32_t climbRateFilterSummation = 0;
  int8_t filterBookmark = climbFilterVals_[0];  // start at the saved spot in the filter array
  int8_t filterIndex =
      filterBookmark;  // and create an index to track all the values we need for averaging

  climbFilterVals_[filterBookmark] = climbRate;  // load in the new value at the bookmarked spot
  if (++filterBookmark > FILTER_VALS_MAX)        // increment bookmark for next time
    filterBookmark = 1;                          // wrap around the array for next time if needed
  climbFilterVals_[0] = filterBookmark;          // and save the bookmark for next time

  // sum up all the values from this spot and previous, for the correct number of samples (user
  // pref)
  for (int i = 0; i < filterValsPref_; i++) {
    climbRateFilterSummation += climbFilterVals_[filterIndex];
    filterIndex--;
    if (filterIndex <= 0) filterIndex = FILTER_VALS_MAX;  // wrap around the array
  }
  climbRateFiltered =
      climbRateFilterSummation / filterValsPref_;  // divide to get the filtered climb rate

  // now calculate the longer-running average climb value (this is a smoother, slower-changing value
  // for things like glide ratio, etc)
  int32_t total_samples =
      CLIMB_AVERAGE * FILTER_VALS_MAX;  // CLIMB_AVERAGE seconds * 20 samples per second = total
                                        // samples to average over

  // current averaege    *   weighted by total samples (minus 1) + one more new sample     / total
  // samples
  climbRateAverage = (climbRateAverage * (total_samples - 1) + climbRateFiltered) / total_samples;

  // finally, update the speaker sound based on the new climbrate
  speaker_updateVarioNote(climbRateFiltered);
}

// ^^^ Device reading & data processing ^^^

// Test Functions

void Barometer::debugPrint() {
  Serial.print("D1_P:");
  Serial.print(D1_P_);
  Serial.print(", D2_T:");
  Serial.print(D2_T_);  // has been zero, perhaps because GPS serial buffer processing delayed the
                        // ADC prep for reading this from baro chip

  Serial.print(", Press:");
  Serial.print(pressure_);
  Serial.print(", PressFiltered:");
  Serial.print(pressureFiltered);
  Serial.print(", PressRegression:");
  Serial.print(pressureRegression_);
  Serial.print(", LastAlt:");
  Serial.print(lastAlt_);
  Serial.print(", ALT:");
  Serial.print(alt);
  Serial.print(", AltAdjusted:");
  Serial.print(altAdjusted);
  Serial.print(", AltSetting:");
  Serial.print(altimeterSetting);
  Serial.print(", CLIMB:");
  Serial.print(climbRate);
  Serial.print(", CLIMB_FILTERED:");
  Serial.println(climbRateFiltered);
}