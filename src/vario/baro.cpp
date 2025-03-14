/*
 * baro.cpp
 *
 */
#include "baro.h"

#include "IMU.h"
#include "Leaf_I2C.h"
#include "LinearRegression.h"
#include "SDcard.h"
#include "buttons.h"
#include "log.h"
#include "settings.h"
#include "speaker.h"
#include "telemetry.h"
#include "tempRH.h"

#define DEBUG_BARO 0  // flag for printing serial debugging messages

// Filter values to average/smooth out the baro sensor

// User Settings for Vario
uint8_t filterValsPref =
    20;  // default samples to average (will be adjusted by VARIO_SENSE user setting)
#define FILTER_VALS_MAX 30  // total array size max; for both altitude and climb
int32_t pressureFilterVals[FILTER_VALS_MAX + 1];  // use [0] as the index / bookmark
int32_t climbFilterVals[FILTER_VALS_MAX + 1];     // use [0] as the index / bookmark

#define CLIMB_AVERAGE \
  4  // number of seconds for average climb rate (this is used for smoother data in places like
     // glide ratio, where rapidly fluctuating glide numbers aren't as useful as a several-second
     // average)

// LinearRegression to average out noisy sensor readings
LinearRegression<20> pressure_lr;

// probably gonna delete these
/*
  #define VARIO_SENSITIVITY 3 // not sure what this is yet :)

  #define PfilterSize		6			// pressure alt filter values (minimum 1,
  max 10) int32_t varioVals[25]; int32_t climbVals[31]; int32_t climbSecVals[9];
*/

// Baro Values
BARO baro;  // struct for common baro values we need all over the place

// Sensor Calibration Values (stored in chip PROM; must be read at startup before performing baro
// calculations)
uint16_t C_SENS;
uint16_t C_OFF;
uint16_t C_TCS;
uint16_t C_TCO;
uint16_t C_TREF;
uint16_t C_TEMPSENS;

// Digital read-out values
uint32_t D1_P;             //  digital pressure value (D1 in datasheet)
uint32_t D1_Plast = 1000;  //  save previous value to use if we ever get a mis-read from the baro
                           //  sensor (initialize with a non zero starter value)
uint32_t D2_T;             //  digital temp value (D2 in datasheet)
uint32_t D2_Tlast = 1000;  //  save previous value to use if we ever get a mis-read from the baro
                           //  sensor (initialize with a non zero starter value)
int32_t lastAlt = 0;

// Temperature Calculations
int32_t dT;

// Compensation Values
int64_t OFF1;   // Offset at actual temperature
int64_t SENS1;  // Sensitivity at actual temperature

// Extra compensation values for lower temperature ranges
int32_t TEMP2;
int64_t OFF2;
int64_t SENS2;

// flag to set first climb rate sample to 0 (this allows us to wait for a second baro altitude
// sample to calculate any altitude change)
bool firstClimbInitialization = true;

void baro_adjustAltSetting(int8_t dir, uint8_t count) {
  float increase = .001;  //
  if (count >= 1) increase *= 10;
  if (count >= 8) increase *= 5;

  if (dir >= 1) {
    baro.altimeterSetting += increase;
    if (baro.altimeterSetting > 32.0) baro.altimeterSetting = 32.0;
  } else if (dir <= -1) {
    baro.altimeterSetting -= increase;
    if (baro.altimeterSetting < 28.0) baro.altimeterSetting = 28.0;
  }
  ALT_SETTING = baro.altimeterSetting;
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

// I2C Communication Functions

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
// I2C Communication Functions

// Device Management

// Initialize the baro sensor
void baro_init(void) {
  // recover saved altimeter setting
  if (ALT_SETTING > 28.0 && ALT_SETTING < 32.0)
    baro.altimeterSetting = ALT_SETTING;
  else
    baro.altimeterSetting = 29.92;

  // reset baro sensor for initialization
  baro_reset();
  delay(2);

  // read calibration values
  C_SENS = baro_readCalibration(1);
  C_OFF = baro_readCalibration(2);
  C_TCS = baro_readCalibration(3);
  C_TCO = baro_readCalibration(4);
  C_TREF = baro_readCalibration(5);
  C_TEMPSENS = baro_readCalibration(6);

  // after initialization, get first baro sensor reading to populate values
  delay(10);             // wait for baro sensor to be ready
  baro_update(1, true);  // send convert-pressure command
  delay(10);             // wait for baro sensor to process
  baro_update(0, true);  // read pressure, send convert-temp command
  delay(10);             // wait for baro sensor to process
  baro_update(0, true);  // read temp, and calculate adjusted pressure
  delay(10);

  // load the filters with our current start-up pressure (and climb is assumed to be 0)
  for (int i = 1; i <= FILTER_VALS_MAX; i++) {
    pressureFilterVals[i] = baro.pressure;
    climbFilterVals[i] = 0;
  }
  // and set bookmark index to 1
  pressureFilterVals[0] = 1;
  climbFilterVals[0] = 1;

  // and save starting filtered output to current pressure
  baro.pressureFiltered = baro.pressure;
  baro.pressureRegression = baro.pressure;

  // and start off the linear regression version
  // pressure_lr.update((double)millis(), (double)baro.pressure);

  baro_update(0, true);  // calculate altitudes

  // initialize all the other alt variables with current altitude to start
  lastAlt = baro.alt;  // used to calculate the alt change for climb rate.  Assume we're stationary
                       // to start (previous Alt = Current ALt, so climb rate is zero).  Note: Climb
                       // rate uses the un-adjusted (standard) altitude
  baro.altInitial = baro.alt;  // also save first value to use as starting point (we assume the
                               // saved altimeter setting is correct for now, so use adjusted)
  baro.altAtLaunch = baro.altAdjusted;  // save the starting value as launch altitude (Launch will
                                        // be updated when timer starts)

  if (DEBUG_BARO) {
    Serial.println("Baro initialization values:");
    Serial.print("  C_SENS:");
    Serial.println(C_SENS);
    Serial.print("  C_OFF:");
    Serial.println(C_OFF);
    Serial.print("  C_TCS:");
    Serial.println(C_TCS);
    Serial.print("  C_TCO:");
    Serial.println(C_TCO);
    Serial.print("  C_TREF:");
    Serial.println(C_TREF);
    Serial.print("  C_TEMPSENS:");
    Serial.println(C_TEMPSENS);
    Serial.print("  D1:");
    Serial.println(D1_P);
    Serial.print("  D2:");
    Serial.println(D2_T);
    Serial.print("  dT:");
    Serial.println(dT);
    Serial.print("  TEMP:");
    Serial.println(baro.temp);
    Serial.print("  OFF1:");
    Serial.println(OFF1);
    Serial.print("  SENS1:");
    Serial.println(SENS1);
    Serial.print("  P_ALT:");
    Serial.println(baro.alt);

    Serial.println(" ");
  }
}

void baro_reset(void) {
  unsigned char command = 0b00011110;  // This is the command to reset, and for the sensor to copy
                                       // calibration data into the register as needed
  baro_sendCommand(command);
  delay(3);  // delay time required before sensor is ready
}

// Reset launcAlt to current Alt (when starting a new log file, for example)
void baro_resetLaunchAlt() {
  baro.altAtLaunch = baro.altAdjusted;
}

// Track if we've put baro to sleep (in power off usb state)
bool baroSleeping = false;
void baro_wake() {
  baroSleeping = false;
}
void baro_sleep() {
  baroSleeping = true;
}

uint32_t baroTimeStampPressure = 0;
uint32_t baroTimeStampTemp = 0;
uint32_t baroADCStartTime = 0;
uint8_t process_step = 0;
bool baroADCBusy = false;
bool baroADCPressure = false;
bool baroADCTemp = false;

void baro_update(bool startNewCycle, bool doTemp) {
  // (we don't need to update temp as frequently so we choose to skip it if desired)
  // the baro senor requires ~9ms between the command to prep the ADC and actually reading the
  // value. Since this delay is required between both pressure and temp values, we break the sensor
  // processing up into several steps, to allow other code to process while we're waiting for the
  // ADC to become ready.

  // only do baro updates if we're not "sleeping" (i.e. in PowerOff state)
  if (baroSleeping) {
    // set climb to 0 so we don't have any vario beeps etc
    baro.climbRate = 0;
    baro.climbRateAverage = 0;
    baro.climbRateFiltered = 0;
    speaker_updateVarioNote(baro.climbRateFiltered);
    return;
  }

  // First check if ADC is not busy (i.e., it's been at least 9ms since we sent a "convert ADC"
  // command)
  unsigned long microsNow = micros();
  if (microsNow - baroADCStartTime > 9000) {
    baroADCBusy = false;
  } else {
    Serial.print("BARO BUSY!  Executing Process Step # ");
    Serial.print(process_step);
    Serial.print("  Micros since last: ");
    Serial.println(microsNow - baroADCStartTime);
  }

  if (startNewCycle) process_step = 0;

  if (DEBUG_BARO) {
    Serial.print("baro step: ");
    Serial.print(process_step);
    Serial.print(" NewCycle? ");
    Serial.print(startNewCycle);
    Serial.print(" time: ");
    Serial.println(micros());
  }

  switch (process_step) {
    case 0:  // SEND CONVERT PRESSURE COMMAND
      if (!baroADCBusy) {
        baroADCStartTime = micros();
        baro_sendCommand(CMD_CONVERT_PRESSURE);  // Prep baro sensor ADC to read raw pressure value
                                                 // (then come back for step 2 in ~10ms)
        baroADCBusy = true;      // ADC will be busy now since we sent a conversion command
        baroADCPressure = true;  // We will have a Pressure value in the ADC when ready
        baroADCTemp =
            false;  // We won't have a Temp value (even if the ADC was holding an unread Temperature
                    // value, we're clearning that out since we sent a Pressure command)
      }
      break;

    case 1:  // READ PRESSURE THEN SEND CONVERT TEMP COMMAND
      if (!baroADCBusy && baroADCPressure) {
        D1_P = baro_readADC();  // Read raw pressure value
        baroADCPressure = false;
        // baroTimeStampPressure = micros() - baroTimeStampPressure; // capture duration between
        // prep and read
        if (D1_P == 0)
          D1_P = D1_Plast;  // use the last value if we get an invalid read
        else
          D1_Plast = D1_P;  // otherwise save this value for next time if needed
        // baroTimeStampTemp = micros();

        if (doTemp) {
          baroADCStartTime = micros();
          baro_sendCommand(CMD_CONVERT_TEMP);  // Prep baro sensor ADC to read raw temperature value
                                               // (then come back for step 3 in ~10ms)
          baroADCBusy = true;
          baroADCTemp = true;       // We will have a Temperature value in the ADC when ready
          baroADCPressure = false;  // We won't have a Pressure value (even if the ADC was holding
                                    // an unread Pressure value, we're clearning that out since we
                                    // sent a Temperature command)
        }
      }
      break;

    case 2:  // READ TEMP THEN CALCULATE ALTITUDE
      if (doTemp) {
        if (!baroADCBusy && baroADCTemp) {
          D2_T = baro_readADC();  // read digital temp data
          baroADCTemp = false;
          // baroTimeStampTemp = micros() - baroTimeStampTemp; // capture duration between prep and
          // read
          if (D2_T == 0)
            D2_T = D2_Tlast;  // use the last value if we get a misread
          else
            D2_Tlast = D2_T;  // otherwise save this value for next time if needed
        }
      }
      // (even if we skipped some steps above because of mis-reads or mis-timing, we can still
      // calculate a "new" corrected pressure value based on the old ADC values.  It will be a
      // repeat value, but it keeps the filter buffer moving on time)
      baro_calculatePressure();  // calculate Pressure adjusted for temperature
      break;

    case 3:  // Filter Pressure and calculate Final Altitude Values
      // baro_filterPressure();
      // baro_calculateAlt();  // filter pressure alt value
      // baro_updateClimb();   // update and filter climb rate
      // if (DEBUG_BARO) baro_debugPrint();

      baro.climbRateFiltered = int32_t(kalmanvert.getVelocity() * 100);
      baro.alt = int32_t(kalmanvert.getPosition() * 100);

      int32_t total_samples = CLIMB_AVERAGE * FILTER_VALS_MAX;

      baro.climbRateAverage =
          (baro.climbRateAverage * (total_samples - 1) + baro.climbRateFiltered) / total_samples;

      // finally, update the speaker sound based on the new climbrate
      speaker_updateVarioNote(baro.climbRateFiltered);

      // if (DEBUG_BARO) { Serial.println("**BR** climbRate Filtered: " +
      // String(baro.climbRateFiltered)); }

      break;
  }
  process_step++;
}

// Device Management

// Device reading & data processing
void baro_calculatePressure() {
  // calculate temperature (in 100ths of degrees C, from -4000 to 8500)
  dT = D2_T - ((int32_t)C_TREF) * 256;
  int32_t TEMP = 2000 + (((int64_t)dT) * ((int64_t)C_TEMPSENS)) / pow(2, 23);

  // calculate sensor offsets to use in pressure & altitude calcs
  OFF1 = (int64_t)C_OFF * pow(2, 16) + (((int64_t)C_TCO) * dT) / pow(2, 7);
  SENS1 = (int64_t)C_SENS * pow(2, 15) + ((int64_t)C_TCS * dT) / pow(2, 8);

  // low temperature compensation adjustments
  TEMP2 = 0;
  OFF2 = 0;
  SENS2 = 0;
  if (TEMP < 2000) {
    TEMP2 = pow((int64_t)dT, 2) / pow(2, 31);
    OFF2 = 5 * pow((TEMP - 2000), 2) / 2;
    SENS2 = 5 * pow((TEMP - 2000), 2) / 4;
  }
  // very low temperature compensation adjustments
  if (TEMP < -1500) {
    OFF2 = OFF2 + 7 * pow((TEMP + 1500), 2);
    SENS2 = SENS2 + 11 * pow((TEMP + 1500), 2) / 2;
  }
  TEMP = TEMP - TEMP2;
  OFF1 = OFF1 - OFF2;
  SENS1 = SENS1 - SENS2;

  // Filter Temp if necessary due to noise in values
  baro.temp = TEMP;  // TODO: actually filter if needed

  // calculate temperature compensated pressure (in 100ths of mbars)
  baro.pressure = ((uint64_t)D1_P * SENS1 / (int64_t)pow(2, 21) - OFF1) / pow(2, 15);

  // record datapoint on SD card if datalogging is turned on

  String baroName = "baro mb*100,";
  String baroEntry = baroName + String(baro.pressure);
  Telemetry.writeText(baroEntry);

  // calculate instant altitude (for kalman filter)
  baro.altF = 44331.0 * (1.0 - pow((float)baro.pressure / 101325.0, (.190264)));
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
void baro_filterPressure(void) {
  // first calculate filter size based on user preference
  switch (VARIO_SENSE) {
    case 1:
      filterValsPref = 30;
      break;
    case 2:
      filterValsPref = 25;
      break;
    case 3:
      filterValsPref = 20;
      break;
    case 4:
      filterValsPref = 15;
      break;
    case 5:
      filterValsPref = 10;
      break;
    default:
      filterValsPref = 20;
      break;
  }

  // new way with regression:
  pressure_lr.update((double)millis(), (double)baro.alt);
  LinearFit fit = pressure_lr.fit();
  baro.pressureRegression = linear_value(&fit, (double)millis());

  // old way with averaging last N values equally:
  baro.pressureFiltered = 0;
  int8_t filterBookmark = pressureFilterVals[0];  // start at the saved spot in the filter array
  int8_t filterIndex =
      filterBookmark;  // and create an index to track all the values we need for averaging

  pressureFilterVals[filterBookmark] =
      baro.pressure;                       // load in the new value at the bookmarked spot
  if (++filterBookmark > FILTER_VALS_MAX)  // increment bookmark for next time
    filterBookmark = 1;                    // wrap around the array for next time if needed
  pressureFilterVals[0] = filterBookmark;  // and save the bookmark for next time

  // sum up all the values from this spot and previous, for the correct number of samples (user
  // pref)
  for (int i = 0; i < filterValsPref; i++) {
    baro.pressureFiltered += pressureFilterVals[filterIndex];
    filterIndex--;
    if (filterIndex <= 0) filterIndex = FILTER_VALS_MAX;  // wrap around the array
  }
  baro.pressureFiltered /= filterValsPref;  // divide to get the average
}

void baro_calculateAlt() {
  // calculate altitude in cm
  baro.alt = 4433100.0 * (1.0 - pow((float)baro.pressureFiltered / 101325.0,
                                    (.190264)));  // standard altimeter setting

  baro.altAdjusted =
      4433100.0 * (1.0 - pow((float)baro.pressureFiltered / (baro.altimeterSetting * 3386.389),
                             (.190264)));  // adjustable altimeter setting
  baro.altAboveLaunch = baro.altAdjusted - baro.altAtLaunch;
}

// Update Climb
void baro_updateClimb() {
  // lastAlt isn't set yet on boot-up, so just assume a zero climb rate for the first sample.
  if (firstClimbInitialization) {
    baro.climbRate = 0;
    baro.climbRateFiltered = 0;
    baro.climbRateAverage = 0;
    firstClimbInitialization = false;
    return;
  }

  // TODO: incorporate ACCEL for added precision/accuracy

  // calculate climb rate based on standard altimeter setting (this way climb doesn't artificially
  // change if the setting is adjusted)
  baro.climbRate =
      (baro.alt - lastAlt) *
      20;  // climb is updated every 1/20 second, so climb rate is cm change per 1/20sec * 20
  lastAlt = baro.alt;  // store last alt value for next time

  // filter climb rate
  int32_t climbRateFilterSummation = 0;
  int8_t filterBookmark = climbFilterVals[0];  // start at the saved spot in the filter array
  int8_t filterIndex =
      filterBookmark;  // and create an index to track all the values we need for averaging

  climbFilterVals[filterBookmark] = baro.climbRate;  // load in the new value at the bookmarked spot
  if (++filterBookmark > FILTER_VALS_MAX)            // increment bookmark for next time
    filterBookmark = 1;                 // wrap around the array for next time if needed
  climbFilterVals[0] = filterBookmark;  // and save the bookmark for next time

  // sum up all the values from this spot and previous, for the correct number of samples (user
  // pref)
  for (int i = 0; i < filterValsPref; i++) {
    climbRateFilterSummation += climbFilterVals[filterIndex];
    filterIndex--;
    if (filterIndex <= 0) filterIndex = FILTER_VALS_MAX;  // wrap around the array
  }
  baro.climbRateFiltered =
      climbRateFilterSummation / filterValsPref;  // divide to get the filtered climb rate

  // now calculate the longer-running average climb value (this is a smoother, slower-changing value
  // for things like glide ratio, etc)
  int32_t total_samples =
      CLIMB_AVERAGE * FILTER_VALS_MAX;  // CLIMB_AVERAGE seconds * 20 samples per second = total
                                        // samples to average over

  // current averaege    *   weighted by total samples (minus 1) + one more new sample     / total
  // samples
  baro.climbRateAverage =
      (baro.climbRateAverage * (total_samples - 1) + baro.climbRateFiltered) / total_samples;

  // finally, update the speaker sound based on the new climbrate
  speaker_updateVarioNote(baro.climbRateFiltered);
}

// Device reading & data processing

// Test Functions

void baro_debugPrint() {
  Serial.print("D1_P:");
  Serial.print(D1_P);
  Serial.print(", D2_T:");
  Serial.print(D2_T);  // has been zero, perhaps because GPS serial buffer processing delayed the
                       // ADC prep for reading this from baro chip

  Serial.print(", Press:");
  Serial.print(baro.pressure);
  Serial.print(", PressFiltered:");
  Serial.print(baro.pressureFiltered);
  Serial.print(", PressRegression:");
  Serial.print(baro.pressureRegression);
  Serial.print(", LastAlt:");
  Serial.print(lastAlt);
  Serial.print(", ALT:");
  Serial.print(baro.alt);
  Serial.print(", AltAdjusted:");
  Serial.print(baro.altAdjusted);
  Serial.print(", AltSetting:");
  Serial.print(baro.altimeterSetting);
  Serial.print(", CLIMB:");
  Serial.print(baro.climbRate);
  Serial.print(", CLIMB_FILTERED:");
  Serial.println(baro.climbRateFiltered);
}