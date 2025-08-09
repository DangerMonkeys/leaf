/*
 * baro.h
 *
 */

#pragma once

#include <Arduino.h>
#include "etl/message_bus.h"

#include "dispatch/message_types.h"
#include "hardware/Leaf_SPI.h"
#include "math/linear_regression.h"
#include "math/running_average.h"
#include "ui/input/buttons.h"
#include "units/pressure.h"
#include "utils/flags_enum.h"

#define FILTER_VALS_MAX 20  // total array size max;
#define DEFAULT_SAMPLES_TO_AVERAGE 3

enum class BarometerTask : uint8_t {
  None,
  Measure,
  FilterAltitude,
};

// Barometer reporting altitude, adjusted altitude, climb rate, and other information.
// Requires a pressure source.
class Barometer : public etl::message_router<Barometer, PressureUpdate> {
 public:
  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // etl::message_router<Barometer, PressureUpdate>
  void on_receive(const PressureUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  Pressure pressure;
  Pressure pressureFiltered;
  float altimeterSetting = 29.921;
  // cm raw pressure altitude calculated off standard altimeter setting (29.92)
  int32_t alt;
  // m raw pressure altitude (float)
  float altF;
  // cm pressure altitude corrected by the altimeter setting (int)
  int32_t altAdjusted;
  int32_t altAtLaunch;
  int32_t altAboveLaunch;
  int32_t altInitial;
  // instantaneous climbrate calculated with every pressure altitude measurement (m/s)
  float climbRate;
  // filtered climb value to reduce noise (cm/s)
  int32_t climbRateFiltered;
  // long-term (several seconds) averaged climb rate for smoothing out glide ratio and other
  // calculations (cm/s)
  float climbRateAverage;
  // TODO: not yet used, but we may have a differently-averaged/filtered value for the grpahical
  // vario bar vs the text output for climbrate
  int32_t varioBar;

  // == Device Management ==
  // Initialize the baro
  void init(void);

  // Reset launcAlt to current Alt (when starting a new log file, for example)
  void resetLaunchAlt(void);
  // Change the number of samples over which pressure and climb rate are averaged
  void setFilterSamples(size_t nSamples);

  void startMeasurement();
  void update();

  void sleep(void);
  void wake(void);

  // == Device reading & data processing ==
  void adjustAltSetting(int8_t dir, uint8_t count);
  // solve for the altimeter setting required to make corrected-pressure-altitude match gps-altitude
  bool syncToGPSAlt(void);

  bool hasFirstReading() { return hasFirstReading_; }

 private:
  int32_t pressureRegression_;

  // LinearRegression to average out noisy sensor readings
  LinearRegression<20> pressureLR_;

  // == User Settings for Vario ==

  RunningAverage<float, FILTER_VALS_MAX> pressureFilter{DEFAULT_SAMPLES_TO_AVERAGE};
  RunningAverage<float, FILTER_VALS_MAX> climbFilter{DEFAULT_SAMPLES_TO_AVERAGE};

  // == Device Management ==

  // Track if we've put baro to sleep (in power off usb state)
  bool sleeping_ = false;

  BarometerTask task_ = BarometerTask::None;

  void firstReading(const PressureUpdate& msg);

  // == Device reading & data processing ==
  void calculatePressureAlt(int32_t newPressure);
  void filterClimb(void);
  void calculateAlts(void);
  void filterPressure(void);  // TODO: Use or remove (currently unused)

  // ======

  int32_t lastAlt_ = 0;

  // flag to set first climb rate sample to 0 (this allows us to wait for a second baro altitude
  // sample to calculate any altitude change)
  bool firstClimbInitialization_ = true;

  // Whether the first reading has been obtained, and therefore whether initialization is complete
  bool hasFirstReading_ = false;
};
extern Barometer baro;

// Conversion functions
int32_t baro_altToUnits(int32_t alt_input, bool units_feet);
float baro_climbToUnits(int32_t climbrate, bool units_fpm);
