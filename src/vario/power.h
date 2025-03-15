/*
 * power.h
 *
 * Battery Charging Chip BQ24073
 *
 */
#ifndef power_h
#define power_h

#include <Arduino.h>

// Pinout for Leaf V3.2.0
#define POWER_CHARGE_I1 41  // 39 on old V3.2.0
#define POWER_CHARGE_I2 42  // 40 on old V3.2.0
#define POWER_LATCH 48
#define POWER_CHARGE_GOOD 47  // INPUT
#define BATT_SENSE 1          // INPUT ADC

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

struct POWER {
  int8_t batteryPercent;  // battery percentage remaining from 0-100%
  uint16_t batteryMV;     // milivolts battery voltage (typically between 3200 and 4200)
  uint16_t batteryADC;    // ADC raw output from ESP32 input pin
  bool charging = false;  // if system is being charged or not
  power_on_states onState = POWER_OFF;
  power_input_levels inputCurrent = i500mA;
};
extern POWER power;

void power_bootUp(void);
void power_init(void);
void power_latch_on(void);
void power_latch_off(void);
void power_shutdown(void);

void power_init_peripherals(void);
void power_sleep_peripherals(void);
void power_wake_peripherals(void);
void power_switchToOnState(void);

void power_update(void);
bool power_autoOff();
void power_resetAutoOffCounter(void);

void power_adjustInputCurrent(int8_t offset);
void power_setInputCurrent(power_input_levels current);
void power_readBatteryState(void);

#endif