/*
 * baro.h
 *
 */

#pragma once

#include <Arduino.h>
#include "etl/message_bus.h"

#include "dispatch/message_types.h"
#include "hardware/power_control.h"
#include "math/linear_regression.h"
#include "math/running_average.h"
// #include "ui/input/buttons.h"
#include "units/pressure.h"
// #include "utils/flags_enum.h"
#include "utils/state_assert_mixin.h"

#define FILTER_VALS_MAX 20  // total array size max;
#define DEFAULT_SAMPLES_TO_AVERAGE 3

// Barometer reporting altitude, adjusted altitude, climb rate, and other information.
// Requires a pressure source.
class Barometer : public etl::message_router<Barometer, PressureUpdate>,
                  public IPowerControl,
                  private StateAssertMixin<Barometer> {
 public:
  enum class State : uint8_t { Uninitialized, WaitingForFirstReading, Ready, Sleeping };

  inline State state() const { return state_; }

  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // etl::message_router<Barometer, PressureUpdate>
  void on_receive(const PressureUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  // IPowerControl
  void sleep();
  void wake();

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

  // == State adjustments ==

  // Change the number of samples over which pressure and climb rate are averaged
  void setFilterSamples(size_t nSamples);

  // Reset launcAlt to current Alt (when starting a new log file, for example)
  void resetLaunchAlt(void);

  // Incrementally adjust altitude (generally from user input)
  void adjustAltSetting(int8_t dir, uint8_t count);

  // Solve for the altimeter setting required to make corrected-pressure-altitude match gps-altitude
  bool syncToGPSAlt(void);

 private:
  State state_ = State::Uninitialized;

  int32_t pressureRegression_;

  // LinearRegression to average out noisy sensor readings
  LinearRegression<20> pressureLR_;

  // == User Settings for Vario ==

  RunningAverage<float, FILTER_VALS_MAX> climbFilter{DEFAULT_SAMPLES_TO_AVERAGE};

  void onUnexpectedState(const char* action, State actual) const;
  friend struct StateAssertMixin<Barometer>;

  // == Device Management ==

  // Initialize the baro
  void init(void);

  void filterAltitude();

  void firstReading(const PressureUpdate& msg);

  // == Device reading & data processing ==
  void calculatePressureAlt(int32_t newPressure);
  void filterClimb(void);
  void calculateAlts(void);

  // ======

  int32_t lastAlt_ = 0;

  // flag to set first climb rate sample to 0 (this allows us to wait for a second baro altitude
  // sample to calculate any altitude change)
  bool firstClimbInitialization_ = true;
};
extern Barometer baro;

// Conversion functions
int32_t baro_altToUnits(int32_t alt_input, bool units_feet);
float baro_climbToUnits(int32_t climbrate, bool units_fpm);
