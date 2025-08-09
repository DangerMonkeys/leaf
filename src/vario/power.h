/*
 * power.h
 *
 * Battery Charging Chip BQ24073
 *
 */
#ifndef power_h
#define power_h

#include <Arduino.h>

// Pinout for Leaf V3.2.0+
#define POWER_LATCH 48
#define BATT_SENSE 1  // INPUT ADC

// Battery Threshold values
#define BATT_FULL_MV 4080   // mV full battery on which to base % full (100%)
#define BATT_EMPTY_MV 3250  // mV empty battery on which to base % full (0%)
#define BATT_SHUTDOWN_MV 3200
// mV at which to shutdown the system to prevent battery over-discharge
// Note: the battery apparently also has over discharge protection, but we don't fully trust it,
// plus we want to shutdown while we have power to save logs etc

// Auto-Power-Off Threshold values
#define AUTO_OFF_MAX_SPEED 3   // mph max -- must be below this speed for timer to auto-stop
#define AUTO_OFF_MAX_ACCEL 10  // Max accelerometer signal
#define AUTO_OFF_MAX_ALT 400   // cm altitude change for timer auto-stop
#define AUTO_OFF_MIN_SEC 20    // seconds of low speed / low accel for timer to auto-stop

enum power_on_states {
  POWER_OFF,  // power off we'll never use, because chip is unpowered and not running
  POWER_ON,   // system is ON by user input (pushbutton) and should function normally.  Dislpay
             // battery icon and charging state depending on what charger is doing (we can't tell if
             // USB is plugged in or not, only if battery is charging or not)
  POWER_OFF_USB  // system is OFF, but has USB power.  Keep power usage to a minimum, and just power
                 // on display to show battery charge state (when charging) or turn display off and
                 // have processor sleep (when not charging)
};  //   ... note: we enter POWER_OFF_USB state either from POWER_OFF and then plugging in to USB
    //   power (no power button detected during boot) or from POWER_ON with USB plugged in, and user
    //   turning off power via pushbutton.

// iMax set by ILIM pin resistor on battery charger chip. Results in 1.348Amps max input (for
// battery charging AND system load) Note: with this higher input limit, the battery charging will
// then be limited by the ISET pin resistor value, to approximately 810mA charging current)
enum power_input_levels { iStandby, i100mA, i500mA, iMax };

class Power {
 public:
  void bootUp();

  void shutdown();

  void switchToOnState();

  void update();

  void resetAutoOffCounter();

  void adjustInputCurrent(int8_t offset);

  void readBatteryState();

  int8_t batteryPercent;  // battery percentage remaining from 0-100%
  uint16_t batteryMV;     // milivolts battery voltage (typically between 3200 and 4200)
  uint16_t batteryADC;    // ADC raw output from ESP32 input pin
  bool charging = false;  // if system is being charged or not
  bool USBinput = false;  // if system is plugged into USB power or not
  power_on_states onState = POWER_OFF;
  power_input_levels inputCurrent = i500mA;

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

  void initPeripherals();
  void sleepPeripherals();
  void wakePeripherals();

  bool autoOff();

  void setInputCurrent(power_input_levels current);
};
extern Power power;

#endif