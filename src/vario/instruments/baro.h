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

  State state() const { return state_; }

  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // etl::message_router<Barometer, PressureUpdate>
  void on_receive(const PressureUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  // IPowerControl
  void sleep();
  void wake();

  // Most recent instantaneous pressure in 100ths of mbar
  Pressure pressure() const;

  Pressure pressureFiltered;
  float altimeterSetting = 29.921;
  // raw pressure altitude in meters with standard altimeter setting (29.92)
  float altF();

  // raw pressure altitude in cm with standard altimeter setting (29.92)
  int32_t alt();

  // pressure altitude in cm corrected by the altimeter setting
  int32_t altAdjusted();

  // instantaneous climbrate calculated with every pressure altitude measurement (m/s)
  float climbRate();

  // filtered climb value to reduce noise (cm/s)
  int32_t climbRateFiltered();

  bool climbRateFilteredValid();

  // long-term (several seconds) averaged climb rate for smoothing out glide ratio and other
  // calculations (cm/s)
  float climbRateAverage();

  bool climbRateAverageValid();

  // == State adjustments ==

  // Change the number of samples over which pressure and climb rate are averaged
  void setFilterSamples(size_t nSamples);

  // Incrementally adjust altitude (generally from user input)
  void adjustAltSetting(int8_t dir, uint8_t count);

  // Solve for the altimeter setting required to make corrected-pressure-altitude match gps-altitude
  bool syncToGPSAlt(void);

  // == Tracking of reference altitudes ==
  // TODO: Do not track these reference altitudes here; instead encapsulate information about what
  // they're used for in a representation of that thing.  For instance, encapsulate a flight
  // (including conditions of launch like time, altitude, etc) in a Flight class.

  // Set launch altitude to current altitude (when starting a new log file, for example)
  void setLaunchAlt();

  // pressure altitude of launch in cm corrected by the altimeter setting
  int32_t altAtLaunch();

  // pressure altitude above launch in cm corrected by the altimeter setting
  int32_t altAboveLaunch() { return altAdjusted() - altAtLaunch(); }

  void setAltInitial();

  // pressure altitude above initial in cm corrected by the altimeter setting
  int32_t altAboveInitial();

 private:
  State state_ = State::Uninitialized;

  Pressure pressure_;

  float altF_;
  bool validAltF_ = false;

  float climbRateRaw_;
  bool validClimbRateRaw_ = false;

  int32_t climbRateFiltered_;
  bool validClimbRateFiltered_ = false;

  // Current representation of average climb rate, or a temporary sum of climb rate samples during
  // initialization
  float climbRateAverage_;
  // Number of remaining initial samples to be summed into climbRateAverage_ before declaring
  // climbRateAverage available
  size_t nInitSamplesRemaining_;

  int32_t altAdjusted_;
  bool validAltAdjusted_ = false;

  int32_t altAtLaunch_;
  bool validAltAtLaunch_ = false;

  int32_t altInitial_;
  bool validAltInitial_ = false;

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
  void setPressureAlt(int32_t newPressure);
  void filterClimb(void);
  void calculateAlts(void);
};
extern Barometer baro;

// Conversion functions
int32_t baro_altToUnits(int32_t alt_input, bool units_feet);
float baro_climbToUnits(int32_t climbrate, bool units_fpm);
