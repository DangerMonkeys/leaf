/*
 * power.h
 *
 * Battery Charging Chip BQ24073
 *
 */
#pragma once

#include <Arduino.h>

#include "utils/constrained_enum.h"

/// @details ... note: we enter PowerState::OffUSB either from Off and then plugging in to USB power
/// (no power button detected during boot) or from On with USB plugged in, and user turning off
/// power via pushbutton.
enum class PowerState : uint8_t {
  Off,  // power off we'll never use, because chip is unpowered and not running

  /// @brief system is ON by user input (pushbutton) and should function normally.
  /// @details Display battery icon and charging state depending on what charger is doing (we can't
  /// tell if USB is plugged in or not, only if battery is charging or not)
  On,

  /// @brief system is OFF, but has USB power.
  /// @details  Keep power usage to a minimum, and just power on display to show battery charge
  /// state (when charging) or turn display off and have processor sleep (when not charging)
  OffUSB
};

// iMax set by ILIM pin resistor on battery charger chip. Results in 1.348Amps max input (for
// battery charging AND system load) Note: with this higher input limit, the battery charging will
// then be limited by the ISET pin resistor value, to approximately 810mA charging current)
enum class PowerInputLevel : uint8_t { Standby = 0, i100mA = 1, i500mA = 2, Max = 3 };
DEFINE_CLAMPING_BOUNDS(PowerInputLevel, PowerInputLevel::Standby, PowerInputLevel::Max);

class Power {
 public:
  struct Info {
    int8_t batteryPercent;  // battery percentage remaining from 0-100%
    uint16_t batteryMV;     // milivolts battery voltage (typically between 3200 and 4200)
    uint16_t batteryADC;    // ADC raw output from ESP32 input pin
    uint16_t battMvCal;     // Calibrated battery voltage (milivolts)
    bool charging = false;  // if system is being charged or not
    bool USBinput = false;  // if system is plugged into USB power or not
    PowerState onState = PowerState::Off;
    PowerInputLevel inputCurrent = PowerInputLevel::i500mA;
    uint16_t chargeCurrentMA;  // measured charge current in mA (if used -- versions 3.2.6+)
  };

  const Info& info() { return info_; }

  void bootUp();

  void shutdown();

  void switchToOnState();

  void update();

  void resetAutoOffCounter();

  void increaseInputCurrent();
  void decreaseInputCurrent();

  // Read battery voltage
  void readBatteryState();

 private:
  // Initialize the power system itself (battery charger and 3.3V regulator etc)
  void initPowerSystem();

  /// @brief latch or unlatch 3.3V regulator.
  /// @details 3.3V regulator may be 'on' due to USB power or user holding power switch down.  But
  /// if Vario is in "ON" state, we need to latch so user can let go of power button and/or unplug
  /// USB and have it stay on
  void latchOn();

  // If no USB power is available, systems will immediately lose power and shut down (after user
  // lets go of center button)
  void latchOff();

  // Start bus log on "startup" (which can happen in multiple places) if configured
  void maybeStartBusLog();

  void initPeripherals();
  void sleepPeripherals();
  void wakePeripherals();

  bool autoOff();

  void setInputCurrent(PowerInputLevel current);

  Info info_;

  // check if we should turn off from  inactivity
  uint8_t autoOffCounter_ = 0;
  int32_t autoOffAltitude_ = 0;

  uint16_t batteryPercentLast_;
};
extern Power power;
