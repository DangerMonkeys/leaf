// Includes
#include "power.h"

#include "hardware/Leaf_I2C.h"
#include "hardware/Leaf_SPI.h"
#include "hardware/aht20.h"
#include "hardware/configuration.h"
#include "hardware/icm_20948.h"
#include "hardware/io_pins.h"
#include "hardware/ms5611.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

POWER power;  // struct for battery-state and on-state variables

void blinkLED(uint8_t count) {
#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);  // LED power status indicator
  for (int i = 0; i < count; i++) {
    delay(500);
    digitalWrite(LED_PIN, LOW);  // turn on LED
    delay(500);
    digitalWrite(LED_PIN, HIGH);  // turn off LED
  }
#endif
}

void power_bootUp() {
  // Init Peripheral Busses
  wire_init();
  Serial.println(" - Finished I2C Wire");
  spi_init();
  Serial.println(" - Finished SPI");

  // initialize IO expander (needed for speaker in v3.2.5+ and power supply in v3.2.6+)
#ifdef HAS_IO_EXPANDER
  ioexInit();  // initialize IO Expander
  Serial.println(" - Finished IO Expander");
#endif

  power_init();  // configure power supply

  // grab user settings (or populate defaults if no saved settings)
  settings.init();

  // initialize Buttons and check if holding the center button is what turned us on
  auto button = buttons_init();

  // go to "ON" state if button (user input) or BOOT_TO_ON flag from firmware update
  if (button == Button::CENTER || settings.boot_toOnState) {
    settings.boot_toOnState = false;
    settings.save();

    power.onState = POWER_ON;

    display.showOnSplash();  // show the splash screen if user turned us on

  } else {
    // if not center button, then USB power turned us on, go into charge mode
    power.onState = POWER_OFF_USB;
  }

  // init peripherals (even if we're not turning on and just going into
  // charge mode, we still need to initialize devices so we can put some
  // of them back to sleep)
  power_init_peripherals();
}

// Initialize the power system itself (battery charger and 3.3V regulator etc)
void power_init() {
  Serial.print("power_init: ");
  Serial.println(power.onState);

  // Set output / input pins to control battery charge and power supply
  pinMode(BATT_SENSE, INPUT);
  pinMode(POWER_LATCH, OUTPUT);
  if (!POWER_CHARGE_I1_IOEX) pinMode(POWER_CHARGE_I1, OUTPUT);
  if (!POWER_CHARGE_I2_IOEX) pinMode(POWER_CHARGE_I2, OUTPUT);
  if (!POWER_CHARGE_GOOD_IOEX) pinMode(POWER_CHARGE_GOOD, INPUT_PULLUP);
  // POWER_GOOD is only available on v3.2.6+ on IOexpander, so not set here

  // set default current limit for charger input
  power_setInputCurrent(i500mA);

#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);  // LED power status indicator
#endif
}

void power_init_peripherals() {
  Serial.print("init_peripherals: ");
  Serial.println(power.onState);

  // initialize speaker to play sound (so user knows they can let go of the power button)
  speaker.init();
  Serial.println(" - Finished Speaker");

  if (power.onState == POWER_ON) {
    power_latch_on();
    speaker.playSound(fx::enter);
    // loop until sound is done playing
    while (speaker.update()) {
      delay(10);
    }
  } else {
    power_latch_off();  // turn off 3.3V regulator (if we're plugged into USB, we'll stay on)
  }

  // then initialize the rest of the devices
  sdcard.init();
  Serial.println(" - Finished SDcard");
  gps.init();
  Serial.println(" - Finished GPS");
  wire_init();
  Serial.println(" - Finished I2C Wire");
  display.init();
  Serial.println(" - Finished display");
  ms5611.init();
  baro.init();
  Serial.println("     Waiting for first pressure reading...");
  // TODO: don't block here; instead, have clients recognize and handle a not-fully-initialized baro
  // appropriately
  while (!baro.hasFirstReading()) {
    ms5611.update();
  }
  Serial.println(" - Finished Baro");
  ICM20948::getInstance().init();
  imu.init();
  Serial.println(" - Finished IMU");
  AHT20::getInstance().init();
  Serial.println(" - Finished Temp Humid");

  // then put devices to sleep if we're in POWER_OFF_USB state
  // (plugged into USB but vario not actively turned on)
  if (power.onState == POWER_OFF_USB) {
    power_sleep_peripherals();
  }
  Serial.println(" - DONE");
}

void power_sleep_peripherals() {
  Serial.print("sleep_peripherals: ");
  Serial.println(power.onState);
  // TODO: all the rest of the peripherals not needed while charging
  Serial.println(" - Sleeping GPS");
  gps.sleep();
  Serial.println(" - Sleeping baro");
  baro.sleep();
  Serial.println(" - Sleeping speaker");
  speaker.mute();
  Serial.println("Shut down speaker");
  Serial.println(" - DONE");
}

void power_wake_peripherals() {
  Serial.println("wake_peripherals: ");
  sdcard.mount();  // re-initialize SD card in case card state was changed while in charging/USB
                   // mode
  Serial.println(" - waking GPS");
  gps.wake();
  Serial.println(" - waking baro and IMU");
  baro.wake();
  Serial.println("     waiting for first pressure reading...");
  // TODO: don't block here; instead, have clients recognize and handle a not-fully-initialized baro
  // appropriately
  while (!baro.hasFirstReading()) {
    ms5611.update();
  }
  imu.wake();
  Serial.println(" - waking speaker");
  speaker.unMute();
  Serial.println(" - DONE");
}

void power_switchToOnState() {
  power_latch_on();
  Serial.println("switch_to_on_state");
  power.onState = POWER_ON;
  power_wake_peripherals();
}

void power_shutdown() {
  Serial.println("power_shutdown");

  display.clear();
  display_off_splash();
  baro.sleep();  // stop getting climbrate updates so we don't hear vario beeps while shutting down

  // play shutdown sound
  speaker.playSound(fx::exit);

  // loop until sound is done playing
  while (speaker.update()) {
    delay(10);
  }

  // saving logs and system data
  if (flightTimer_isRunning()) {
    flightTimer_stop();
  }

  // save any changed settings this session
  settings.save();

  // wait another 2.5 seconds before shutting down to give user
  // a chance to see the shutdown screen
  delay(2500);

  // finally, turn off devices
  power_sleep_peripherals();
  display.clear();
  delay(100);
  power_latch_off();  // turn off 3.3V regulator (if we're plugged into USB, we'll stay on)
  delay(100);

  // go to POWER_OFF_USB state, in case device was shut down while
  // plugged into USB, then we can show necessary charging updates etc
  power.onState = POWER_OFF_USB;
}

// latch or unlatch 3.3V regulator.
// 3.3V regulator may be 'on' due to USB power or user holding power switch down.  But if Vario is
// in "ON" state, we need to latch so user can let go of power button and/or unplug USB and have it
// stay on
void power_latch_on() { digitalWrite(POWER_LATCH, HIGH); }

// If no USB power is available, systems will immediately lose
// power and shut down (after user lets go of center button)
void power_latch_off() { digitalWrite(POWER_LATCH, LOW); }

void power_update() {
  // first check for USB power and turn on LED if so
  // (only for v3.2.6+ with controllable LED and PowerGood input)
#ifdef LED_PIN
  power.USBinput = !ioexDigitalRead(POWER_GOOD_IOEX, POWER_GOOD);
  if (power.USBinput) {
    digitalWrite(LED_PIN, LOW);  // turn on LED if charging
  } else {
    digitalWrite(LED_PIN, HIGH);  // turn off LED if not charging
  }
#endif

  // update battery state
  power_readBatteryState();

  // check if we should shut down due to low battery..
  // TODO: should we only do this if we're NOT charging?
  //       Probably not, since shutting down allows more current for charging
  if (power.batteryMV <= BATT_SHUTDOWN_MV) {
#ifndef DISABLE_BATTERY_SHUTDOWN
    power_shutdown();
#endif

    // ..or if we should shutdown due to inactivity
    // (only check if user setting is on and flight timer is stopped)
  } else if (settings.system_autoOff && !flightTimer_isRunning()) {
    // check if inactivity conditions are met
    if (power_autoOff()) {
      power_shutdown();  // shutdown!
    }
  }
}

// check if we should turn off from  inactivity
uint8_t autoOffCounter = 0;
int32_t autoOffAltitude = 0;

void power_resetAutoOffCounter() { autoOffCounter = 0; }

bool power_autoOff() {
  bool autoShutOff = false;  // start with assuming we're not going to turn off

  // we will auto-stop only if BOTH the GPS speed AND the Altitude change trigger the stopping
  // thresholds.

  // First check if altitude is stable
  int32_t altDifference = baro.alt - autoOffAltitude;
  if (altDifference < 0) altDifference *= -1;
  if (altDifference < AUTO_OFF_MAX_ALT) {
    // then check if GPS speed is slow enough
    if (gps.speed.mph() < AUTO_OFF_MAX_SPEED) {
      autoOffCounter++;
      if (autoOffCounter >= AUTO_OFF_MIN_SEC) {
        autoShutOff = true;
      } else if (autoOffCounter >= AUTO_OFF_MIN_SEC - 5) {
        speaker.playSound(
            fx::decrease);  // start playing warning sounds 5 seconds before it auto-turns off
      }
    } else {
      autoOffCounter = 0;
    }

  } else {
    autoOffAltitude += altDifference;  // reset the comparison altitude to present altitude, since
                                       // it's still changing
  }

  Serial.print(
      "                                                   *************AUTO OFF***  Counter: ");
  Serial.print(autoOffCounter);
  Serial.print("   Alt Diff: ");
  Serial.print(altDifference);
  Serial.print("  AutoShutOff? : ");
  Serial.println(autoShutOff);

  return autoShutOff;
}

// Read battery voltage
uint16_t batteryPercentLast;
void power_readBatteryState() {
  // Update Charge State
  power.charging = !ioexDigitalRead(POWER_CHARGE_GOOD_IOEX, POWER_CHARGE_GOOD);
  // logic low is charging, logic high is not

  // Battery Voltage Level & Percent Remaining
  power.batteryADC = analogRead(BATT_SENSE);
  // uint16_t batt_level_mv = adc_level * 5554 / 4095;  //    (3300mV ADC range / .5942 V_divider) =
  // 5554.  Then divide by 4095 steps of resolution

  // adjusted formula to account for ESP32 ADC non-linearity; based on calibration
  // measurements.  This is most accurate between 2.4V and 4.7V
  power.batteryMV = power.batteryADC * 5300 / 4095 + 260;

  if (power.batteryMV < BATT_EMPTY_MV) {
    power.batteryPercent = 0;
  } else if (power.batteryMV > BATT_FULL_MV) {
    power.batteryPercent = 100;
  } else {
    power.batteryPercent = 100 * (power.batteryMV - BATT_EMPTY_MV) / (BATT_FULL_MV - BATT_EMPTY_MV);
  }

  // use a 2% hysteresis on battery% to avoid rapid fluctuations as ADC values change
  if (power.charging) {  // don't let % go down (within 2%) when charging
    if (power.batteryPercent < batteryPercentLast &&
        power.batteryPercent >= batteryPercentLast - 2) {
      power.batteryPercent = batteryPercentLast;
    }
  } else {  // don't let % go up (within 2%) when discharging
    if (power.batteryPercent > batteryPercentLast &&
        power.batteryPercent <= batteryPercentLast + 2) {
      power.batteryPercent = batteryPercentLast;
    }
  }
  batteryPercentLast = power.batteryPercent;
}

void power_adjustInputCurrent(int8_t offset) {
  int8_t newVal = power.inputCurrent + offset;
  if (newVal > iMax)
    newVal = iMax;
  else if (newVal < iStandby)
    newVal = iStandby;
  power_setInputCurrent((power_input_levels)newVal);
}

// Note: the Battery Charger Chip has controllable input current (which is then used for both batt
// charging AND system load).  The battery will be charged with whatever current is remaining after
// system load.
void power_setInputCurrent(power_input_levels current) {
  power.inputCurrent = current;
  switch (current) {
    case i100mA:
      ioexDigitalWrite(POWER_CHARGE_I1_IOEX, POWER_CHARGE_I1, LOW);
      ioexDigitalWrite(POWER_CHARGE_I2_IOEX, POWER_CHARGE_I2, LOW);
      break;
    default:
    case i500mA:
      // set I2 to low first, so we don't accidentally have both set to High (if coming from iMax)
      ioexDigitalWrite(POWER_CHARGE_I2_IOEX, POWER_CHARGE_I2, LOW);
      ioexDigitalWrite(POWER_CHARGE_I1_IOEX, POWER_CHARGE_I1, HIGH);
      break;
    case iMax:  // Approx 1.348A max input current (set by ILIM pin resistor value).
                // In this case, battery charging will then be limited by the max fast-charge limit
                // set by the ISET pin resistor value (approximately 810mA to the battery).
      ioexDigitalWrite(POWER_CHARGE_I1_IOEX, POWER_CHARGE_I1, LOW);
      ioexDigitalWrite(POWER_CHARGE_I2_IOEX, POWER_CHARGE_I2, HIGH);
      break;
    case iStandby:  // USB Suspend mode - turns off USB input power (no charging or supplemental
                    // power, but battery can still power the system)
      ioexDigitalWrite(POWER_CHARGE_I1_IOEX, POWER_CHARGE_I1, HIGH);
      ioexDigitalWrite(POWER_CHARGE_I2_IOEX, POWER_CHARGE_I2, HIGH);
      break;
  }
  Serial.print("set input current: ");
  Serial.println(current);
}