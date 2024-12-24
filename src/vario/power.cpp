// Includes
  #include "power.h"
  #include "buttons.h"
  #include "power.h"
  #include "Leaf_SPI.h"
  #include "SDcard.h"
  #include "display.h"
  #include "gps.h"
  #include "baro.h"
  #include "IMU.h"
  #include "speaker.h"
  #include "settings.h"
  #include "log.h"
  #include "Leaf_I2C.h"
  #include "tempRH.h"

uint8_t current_setting;          // keep track of which input current setting we're using
uint8_t powerOnState = POWER_OFF; // keep track of which power state we're in (ON & Runnning; or (soft)OFF and charging via USB, or dead OFF)

/* was here for testing, can delete
void power_simple_init(void) {
  
  #define POWER_CHARGE_I1   41  //  39 Old version 3.2.0
  #define POWER_CHARGE_I2   42  //  40
  #define POWER_LATCH       48  

  // enable 3.3V regulator
  pinMode(POWER_LATCH, OUTPUT);
  digitalWrite(POWER_LATCH, HIGH);

  // set power supply to 500mA mode
  pinMode(POWER_CHARGE_I1, OUTPUT);
  pinMode(POWER_CHARGE_I2, OUTPUT);  
  digitalWrite(POWER_CHARGE_I1, HIGH);
  digitalWrite(POWER_CHARGE_I2, LOW);
}*/

void power_bootUp() {

  power_init();                                     // configure power supply
  auto button = buttons_init();                  // initialize Button and check if holding the center button is what turned us on
  if (button == Button::DOWN) powerOnState = POWER_ON;    // if center button, then latch on and start operating!
  else powerOnState = POWER_OFF_USB;                // if not center button, then USB power turned us on, go into charge mode
  settings_init();                                  // grab user settings (or populate defaults if no saved settings)
  power_init_peripherals();                         // init peripherals (even if we're not turning on and just going into charge mode, we still need to initialize devices so we can put some of them back to sleep)
}

// Initialize the power system itself (battery charger and 3.3V regulator etc)
void power_init() {
    Serial.print("power_init: ");
    Serial.println(powerOnState);
  // Set output / input pins to control battery charge and power supply  
    pinMode(POWER_LATCH, OUTPUT);
    pinMode(POWER_CHARGE_I1, OUTPUT);
    pinMode(POWER_CHARGE_I2, OUTPUT);  
    pinMode(POWER_CHARGE_GOOD, INPUT_PULLUP);
    pinMode(BATT_SENSE, INPUT);
    power_set_input_current(i500mA);  // set default current
}

void power_init_peripherals() {
    Serial.print("init_peripherals: ");
    Serial.println(powerOnState);
  // initialize speaker to play sound (so user knows they can let go of the power button)        
    speaker_init();         Serial.println("Finished Speaker");
    if (powerOnState == POWER_ON) {
      power_latch_on();      
      speaker_playSound(fx_enter);
    }    
    // then initialize the rest of the devices
    SDcard_init();            Serial.println("Finished SDcard");
    gps_init();               Serial.println("Finished GPS");  
    wire_init();              Serial.println("Finished I2C Wire");  
    spi_init();               Serial.println("Finished SPI");
    display_init();           Serial.println("Finished display");   // u8g2 library initializes SPI bus for itself, so this can come before spi_init()
    //TODO: show loading / splash Screen?

    //GLCD_init();            Serial.println("Finished GLCD");    // test SPI object to write directly to LCD (instead of using u8g2 library -- note it IS possible to have both enabled at once)
    baro_init();              Serial.println("Finished Baro");
    imu_init();               Serial.println("Finished IMU"); 
    tempRH_init();       
  

    // then put devices to sleep as needed if we're in POWER_OFF_USB state (plugged into USB but vario not actively turned on)
    if (powerOnState == POWER_OFF_USB) {   
      //delay(35);   
      power_sleep_peripherals();
    }
}

void power_sleep_peripherals() {
  Serial.print("sleep_peripherals: ");
  Serial.println(powerOnState);
  //TODO: all the rest of the peripherals not needed while charging
  gps_sleep();        Serial.println("Sleeping GPS");  
  //speaker_sleep();    Serial.println("Shut down speaker");
}

void power_wake_peripherals() {
  Serial.println("wake_peripherals: ");
  SDcard_mount();                        // re-initialize SD card in case card state was changed while in charging/USB mode
  gps_wake();         
  speaker_wake();
}

void power_switchToOnState() {
  power_latch_on();
  Serial.println("switch_to_on_state");  
  powerOnState = POWER_ON;
  power_wake_peripherals();
}

void power_shutdown() {
  Serial.println("power_shutdown"); 

  // TODO: maybe show shutting down screen?
  display_clear();


  // saving logs and system data  
  if (flightTimer_isRunning()) {
    flightTimer_stop();
  }

  // save any changed settings this session
  settings_save();
  
  power_sleep_peripherals();
  delay(100);
  power_latch_off();  
  // go to POWER_OFF_USB state, in case device was shut down while plugged into USB, then we can show necessary charging updates etc
  powerOnState = POWER_OFF_USB;
}




// latch or unlatch 3.3V regulator.
// 3.3V regulator may be 'on' due to USB power or user holding power switch down.  But if Vario is in "ON" state, we need to latch so user can let go of power button and/or unplug USB and have it stay on
void power_latch_on() { digitalWrite(POWER_LATCH, HIGH); }  // latch 3.3V regulator
void power_latch_off() { digitalWrite(POWER_LATCH, LOW); }  // unlatch 3.3V regulator.  If no USB, systems will immediately lose power and shut down processor (after user lets go of center button)


void power_update() {
  //TODO: fill this in
  // stuff like checking batt state, checking auto-off, etc


  // check if we should auto turn off 
  if (AUTO_OFF && !flightTimer_isRunning()) { // only check if the AUTO_OFF user-setting is ON *AND* the flight timer is NOT running (don't turn off in the middle of a log recording)
    if (power_autoOff()) {
      power_shutdown();
    }
  }
}


// check if we should turn off from  inactivity
uint8_t autoOffCounter = 0;
int32_t autoOffAltitude = 0;

void power_resetAutoOffCounter() {
  autoOffCounter = 0;
}

bool power_autoOff() {
  bool autoShutOff = false;  // start with assuming we're not going to turn off

  // we will auto-stop only if BOTH the GPS speed AND the Altitude change trigger the stopping thresholds.

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
        speaker_playSound(fx_decrease);  // start playing warning sounds 5 seconds before it auto-turns off
      }
    } else {
      autoOffCounter = 0;
    }

  } else {
    autoOffAltitude += altDifference;  //reset the comparison altitude to present altitude, since it's still changing
  }



  Serial.print("                                                   *************AUTO OFF***  Counter: ");
  Serial.print(autoOffCounter);
  Serial.print("   Alt Diff: ");
  Serial.print(altDifference);
  Serial.print("  AutoShutOff? : ");
  Serial.println(autoShutOff);

  return autoShutOff;
}


uint32_t ADC_value;
uint16_t power_getBattLevel(uint8_t value) {
  uint16_t adc_level = analogRead(BATT_SENSE);
  //uint16_t batt_level_mv = adc_level * 5554 / 4095;  //    (3300mV ADC range / .5942 V_divider) = 5554.  Then divide by 4095 steps of resolution
  uint16_t batt_level_mv = adc_level * 5300 / 4095 + 260; // adjusted formula to account for ESP32 ADC non-linearity; based on calibration measurements.  This is most accurate between 2.4V and 4.7V
  uint16_t batt_level_pct;
  if (batt_level_mv < BATT_EMPTY_MV) {
    batt_level_pct = 0;
  } else if (batt_level_mv > BATT_FULL_MV) {
    batt_level_pct = 100;
  } else {
    batt_level_pct = 100 * (batt_level_mv - BATT_EMPTY_MV) / (BATT_FULL_MV - BATT_EMPTY_MV);
  }  
  if (value == 1) return batt_level_mv;  
  else if (value == 2) return adc_level;
  else return batt_level_pct;
}

bool power_getBattCharging() {
  return (!digitalRead(POWER_CHARGE_GOOD)); // logic low is charging, logic high is not
}


void power_adjustInputCurrent(int8_t offset) {
  int8_t newVal = power_getInputCurrent() + offset;
  if (newVal > iMax) newVal = iMax;
  else if (newVal < iStandby) newVal = iStandby;
  power_set_input_current(newVal);
}



uint8_t power_getInputCurrent() {
  return current_setting;
}

// Note: the Battery Charger Chip has controllable input current (which is then used for both batt charging AND system load).  The battery will be charged with whatever current is remaining after system load.
void power_set_input_current(uint8_t current) {
  current_setting = current;
  switch (current) {
    default:
    case i100mA:
      digitalWrite(POWER_CHARGE_I1, LOW);
      digitalWrite(POWER_CHARGE_I2, LOW);
      break;
    case i500mA:
      digitalWrite(POWER_CHARGE_I1, HIGH);
      digitalWrite(POWER_CHARGE_I2, LOW);
      break;
    case iMax:      // Approx 1.348A max input current (set by ILIM pin resistor value).
                    // In this case, battery charging will then be limited by the max fast-charge limit set by the ISET pin resistor value (approximately 810mA to the battery).
      digitalWrite(POWER_CHARGE_I1, LOW);
      digitalWrite(POWER_CHARGE_I2, HIGH);
      break;
    case iStandby:  // USB Suspend mode - turns off USB input power (no charging or supplemental power, but battery can still power the system)
      digitalWrite(POWER_CHARGE_I1, HIGH);
      digitalWrite(POWER_CHARGE_I2, HIGH);
      break;
  }

}


// Testing STuff ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void power_test(void) {

  Serial.print("Batt Voltage: ");
  Serial.print(power_getBattLevel(0));
  Serial.print(", Batt Percent: ");
  Serial.print(power_getBattLevel(1));
  Serial.print("%, Charge_Good: ");
  if (!digitalRead(POWER_CHARGE_GOOD)) Serial.println("Charging!");
  else Serial.println("Not Charging!");

  if (Serial.available() > 0) {
    char letter = char(Serial.read());
    while (Serial.available() > 0) {
      Serial.read(); //empty buffer
    }

    int fx = 0;
    switch (letter) {
      case '1': power_set_input_current(i100mA); Serial.println("Input Current to 100mA"); break;
      case '2': power_set_input_current(i500mA); Serial.println("Input Current to 500mA"); break;
      case '3': power_set_input_current(iMax); Serial.println("Input Current to Max (1348mA input; ~810mA charger)"); break;
      case '0': power_set_input_current(iStandby); Serial.println("Input Current to Standby"); break;
    }
  } 
  delay(500);
}