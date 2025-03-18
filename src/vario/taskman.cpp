// includes
#include <Arduino.h>
#include <HardwareSerial.h>
#include "task.h"

#include "IMU.h"
#include "Leaf_SPI.h"
#include "SDcard.h"
#include "baro.h"
#include "ble.h"
#include "buttons.h"
#include "gps.h"
#include "log.h"
#include "power.h"
#include "settings.h"
#include "speaker.h"
#include "tempRH.h"
#include "ui/display.h"
#include "wind_estimate/wind_estimate.h"

#ifdef MEMORY_PROFILING
#include "memory_report.h"
#endif

#ifdef DEBUG_WIFI
#include <WiFi.h>
#include "DebugWebserver.h"
#endif

//*** Task Manager
// This module is where the bulk of the task management work
// happens.  We are slowly moving away from this into a
// FreeRTOS task based scheduler, so, we expect this file to
// shrink over time
// ******

// Bit of a hack for now as we don't have a good way to pass big chunks
// of data around, so, we do it on the stack.  Default is 8KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);  // 16KB

#define DEBUG_MAIN_LOOP false

// Pinout for ESP32 Leaf V2
#define AVAIL_GPIO_0 0    // unused, broken out to header
#define AVAIL_GPIO_21 21  // unused, broken out to header (also used as LCD backlight if desired)
#define AVAIL_GPIO_39 39  // unused, broken out to header

// Main system event/task timer
hw_timer_t* task_timer = NULL;
#define TASK_TIMER_FREQ 1000  // run at 1000Hz
#define TASK_TIMER_LENGTH 10  // trigger the ISR every 10ms
void onTaskTimer(void);

// Standby system timer (when on USB power and charging battery, but otherwise "off" (i.e., soft
// off))
hw_timer_t* charge_timer = NULL;
#define CHARGE_TIMER_FREQ 1000   // run at 1000Hz
#define CHARGE_TIMER_LENGTH 500  // trigger the ISR every 500ms
void onChargeTimer(void);
char chargeman_doTasks =
    0;  // let the ISR timer set this to 1 so we're on proper timing cycle each time
uint64_t sleepTimeStamp =
    0;  // track micros() for how long we should sleep in charge mode between actions.

// Counters for system task timer
char counter_10ms_block = 0;
char counter_100ms_block = 0;

// flag if the GPS serial port is done sending data for awhile
bool gps_is_quiet = 0;

// Trackers for Task Manager Queue.  Default to tasks being needed, so they execute upon startup
char taskman_setTasks = 1;  // the task of setting tasks -- usually set by timer-driven interrupt

char taskman_buttons = 1;       // poll & process buttons
char taskman_speakerTimer = 1;  // adjust speaker notes
char taskman_baro = 1;  // (1) preprocess on-chip ADC pressure, (2) read pressure and preprocess
                        // on-chip ADC temperature, (3) read temp and calulate true Alt, (4) filter
                        // Alt, update climb, and store values etc
char taskman_imu = 1;   // read sensors and update values
char taskman_gps = 1;   // process any NMEA strings and update values
char taskman_display = 1;  // update display
char taskman_power = 1;    // check battery, check auto-turn-off, etc
char taskman_log = 1;      // check auto-start, increment timers, update log file, etc
char taskman_tempRH = 1;   // (1) trigger temp & humidity measurements, (2) process values and save
char taskman_SDCard = 1;   // check if SD card state has changed and attempt remount if needed
char taskman_memory_stats = 1;  // Prints memory usage reports
char taskman_estimateWind = 1;  // estimate wind speed and direction

// temp testing stuff
// uint8_t display_page = 0;
uint8_t display_do_tracker = 1;
#define TESTING_LOOP false

// Function Prototypes
void taskManager(void);
void main_ON_loop();
void main_CHARGE_loop();
void setTasks();
void taskManager();

/////////////////////////////////////////////////
//             SETUP              ///////////////
/////////////////////////////////////////////////
void taskmanSetup() {
#ifdef DEBUG_WIFI
  // Start WiFi
  WiFi.begin();
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.println("WiFi Event " + WiFi.localIP().toString() + ": " + event);
  });
#endif
#ifdef DEBUG_WIFI
  // Start WebServer
  webserver_setup();
#endif

  // Adjust the priority of this (the loop event)'s FreeRTOS priority
  vTaskPrioritySet(NULL, 10);

  // turn on and handle all device initialization
  power_bootUp();

  // Start Main System Timer for Interrupt Events (this will tell Main Loop to set tasks every
  // interrupt cycle)
  task_timer = timerBegin(TASK_TIMER_FREQ);
  timerAttachInterrupt(
      task_timer,
      &onTaskTimer);  // timer, ISR call          NOTE: timerDetachInterrupt() does the opposite
  timerAlarm(task_timer, TASK_TIMER_LENGTH, true,
             0);  // auto reload timer ever time we've counted a sample length

  // Start Charge System Timer for Interrupt Events (this will tell Main Loop to do tasks every
  // interrupt cycle)
  charge_timer = timerBegin(CHARGE_TIMER_FREQ);
  timerAttachInterrupt(
      charge_timer,
      &onChargeTimer);  // timer, ISR call          NOTE: timerDetachInterrupt() does the opposite
  timerAlarm(charge_timer, CHARGE_TIMER_LENGTH, true,
             0);  // auto reload timer ever time we've counted a sample length

  // All done!
  Serial.println("Finished Setup");

  // Serial.println("Testing SD Card");
  // SDcard_test();
}

// Main Timer Interrupt
void IRAM_ATTR onTaskTimer() {
  // do stuff every alarm cycle (default 10ms)
  taskman_setTasks = 1;  // next time through main loop, set tasks to do!
  // wakeup();  // go back to main loop and keep processing stuff that needs doing!
}

// Charge Timer Interrupt
void IRAM_ATTR onChargeTimer() {
  sleepTimeStamp = micros();
  // do stuff every alarm cycle (default 10ms)
  chargeman_doTasks = 1;  // next time through main loop, set tasks to do!
  // wakeup();  // go back to main loop and keep processing stuff that needs doing!
}

/////////////////////////////////////////////////
// Main Loop for Processing Tasks ///////////////
/////////////////////////////////////////////////
/*
The main loop perioritizes processesing any data in the serial buffer so as not to miss any NMEA GPS
characters. When the serial buffer is empty, the main loop will move on to processing any other
remaining tasks.

The gps serial port is considered 'quiet' if we received the last character of the last NMEA
sentence.  We shouldn't expect more sentences for awhile (~1 second GPS updates) If the serial port
is quiet, and all tasks are done, then the processor will go to sleep for the remainder of the 10ms
time block. Every 10ms, driven by an interrupt timer, the system will wake up, set flags for which
tasks are needed, and then return to running the main loop.

TODO: In addition to the timer-driven interrupt, we may consider also setting wake-up interrupts for
the pushbuttons, the GPS 1PPS signal, and perhaps others.
*/

// LOOP NOTES:
// when re-entering POWER_ON state, be sure to start from tasks #1, so baro ADC can be re-prepped
// before reading

void loop() {
#ifdef DEBUG_WIFI
  webserver_loop();
#endif

  if (power.onState == POWER_ON)
    main_ON_loop();
  else if (power.onState == POWER_OFF_USB)
    main_CHARGE_loop();
  else
    Serial.print("FAILED MAIN LOOP HANDLER");
}

bool goToSleep = false;
uint64_t taskTimeLast = 0;
uint64_t taskTimeNow = 0;
uint32_t taskDuration = 0;

void main_CHARGE_loop() {
  if (chargeman_doTasks) {
    /* //debug print for checking timing
      taskTimeNow = micros();
      taskDuration = taskTimeNow - taskTimeLast;
      taskTimeLast = taskTimeNow;
      //Serial.println(" ");
      //Serial.print("taskDuration: "); Serial.println(taskDuration);
    */

    // Display Charging Page
    display_setPage(page_charging);
    display_update();  // update display based on battery charge state etc

    // Check SD Card State and remount if card was inserted
    SDcard_update();

    // update battery level and charge state
    power_readBatteryState();

    // Check Buttons
    auto buttonPushed =
        buttons_update();  // check Button for any presses (user can turn ON from charging state)

    // Prep to end this cycle and sleep
    chargeman_doTasks = 0;  // done with tasks this timer cycle
    if (buttonPushed == Button::NONE)
      goToSleep = true;  // get ready to sleep if no button is being pushed
  } else {
    if (goToSleep && settings.system_ecoMode) {  // don't allow sleep if ECO_MODE is off

      goToSleep = false;  // we don't want to sleep again as soon as we wake up; we want to wait
                          // until we've done 'doTasks' before sleeping again

      // Wake up if button pushes
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_CENTER, HIGH);
      esp_sleep_enable_ext0_wakeup(
          (gpio_num_t)BUTTON_PIN_LEFT,
          HIGH);  // TODO: we probably only need to wake up with center button
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_RIGHT, HIGH);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_UP, HIGH);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_DOWN, HIGH);

      // or wake up with timer
      uint64_t microsNow = micros();
      uint64_t sleepMicros =
          sleepTimeStamp + (1000 * (CHARGE_TIMER_LENGTH - 1)) -
          microsNow;  // sleep until just 1ms before the expected next cycle of CHARGE_TIMER
      if (sleepMicros > 496 * 1000)
        sleepMicros = 496 * 1000;  // ensure we don't go to sleep for too long if there's some math
                                   // issue (or micros rollover, etc).
      esp_sleep_enable_timer_wakeup(sleepMicros);  // set timer to wake up

      // sleep for real if ECO_MODE is set, otherwise 'fake sleep' using delay
      if (settings.system_ecoMode) {  // TODO: this is doubling the condition since we already check
                                      // ECO_MODE in
                                      // the parent if() statement
        esp_light_sleep_start();
      } else {
        Serial.print("microsNow:    ");
        Serial.println(microsNow);
        Serial.print("sleepMicros:  ");
        Serial.println(sleepMicros);
        Serial.print("wakeMicros:   ");
        Serial.println(microsNow + sleepMicros);
        delayMicroseconds(sleepMicros);  // use delay instead of actual sleep to test sleep logic
                                         // (allows us to serial print during 'fake sleep' and also
                                         // re-program over USB mid-'fake sleep')
      }
    } else {
      // if (Button_update() != NONE) Serial.println("we see a button!");  // TOOD: erase this
    }
  }
}

// uint32_t millisBegin;
void main_ON_loop() {
  if (taskman_setTasks) {  // check flag set by timer interrupt.  We only do this once every 10ms
    // millisBegin = millis();   // save the timestamp when we enter this 10ms block... we'll need
    // to be done within 10ms! Serial.println(millisBegin);
    setTasks();            // set necessary tasks for this 10ms block
    taskman_setTasks = 0;  // we don't need to do this again until the next 10ms timer interrupt
  }

  // do necessary tasks
  taskManager();

  // GPS Serial Buffer Read
  // stop reading if buffer returns empty, OR, if our 10ms time block is up (because main timer
  // interrupt fired and set setTasks to true)
  bool gps_buffer_full = true;
  while (gps_buffer_full && !taskman_setTasks) {
    gps_buffer_full = gps_read_buffer_once();
  }

  // if (gps_is_quiet) goToSleep();
  // else, run this loop again and keep processing the serial buffer until we're done with all the
  // NMEA sentences this cycle

  // TODO: if gps serial buffer can fill while processor is sleeping, then we don't need to wait for
  // serial port to be quiet
}

void setTasks(void) {
  // increment time counters
  if (++counter_10ms_block >= 10) {
    counter_10ms_block = 0;  // every 10 periods of 10ms, go back to 0 (100ms total)
    if (++counter_100ms_block >= 10) {
      counter_100ms_block = 0;  // every 10 periods of 100ms, go back to 0 (1sec total)
    }
  }

  // tasks to complete every 10ms
  taskman_buttons = 1;
  taskman_baro = 1;
  taskman_speakerTimer = 1;

  // set additional tasks to complete, broken down into 10ms block cycles.  (embedded if()
  // statements allow for tasks every second, spaced out on different 100ms blocks)
  switch (counter_10ms_block) {
    case 0:
      baro.startMeasurement();  // begin updating baro every 50ms on the 0th and 5th blocks
      // taskman_baro = 0;  // begin updating baro every 50ms on the 0th and 5th blocks
      break;
    case 1:
      // taskman_baro = 1;
      break;
    case 2:
      // taskman_baro = 2;
      taskman_imu = 1;           // update accel every 50ms during the 2nd & 7th blocks
      taskman_estimateWind = 1;  // estimate wind speed and direction
      break;
    case 3:
      // taskman_baro = 3;
      break;
    case 4:
      break;
    case 5:
      baro.startMeasurement();  // begin updating baro every 50ms on the 0th and 5th blocks
      // taskman_baro = 0;  // begin updating baro every 50ms on the 0th and 5th blocks
      break;
    case 6:
      // taskman_baro = 1;
      break;
    case 7:
      taskman_imu = 1;  // update accel every 50ms during the 2nd & 7th blocks
      // taskman_baro = 2;
      break;
    case 8:
      // taskman_baro = 3
      break;
    case 9:

      // Tasks every second complete here in the 9th 10ms block.  Pick a unique 100ms block for each
      // task to space things out

      if (counter_100ms_block == 0 || counter_100ms_block == 5)
        taskman_gps = 1;  // gps update every half second (this avoids any aliasing issues if we
                          // keep trying to update GPS in the middle of an NMEA sentence string)
      if (counter_100ms_block == 1) taskman_power = 1;  // every second: power checks
      if (counter_100ms_block == 2) taskman_log = 1;    // every second: logging
      if (counter_100ms_block == 3 || counter_100ms_block == 8)
        taskman_display = 1;  // Update LCD every half-second on the 3rd and 8th 100ms blocks
      if (counter_100ms_block == 4)
        taskman_tempRH = 1;  // trigger the start of a new temp & humidity measurement
      // 5 - gps
      if (counter_100ms_block == 6)
        taskman_SDCard = 1;  // check if SD card state has changed and remount if needed
// 7 - available
// 8 - LCD
#ifdef MEMORY_PROFILING
      if (counter_100ms_block == 7) {
        taskman_memory_stats = 1;
      }
#endif
      if (counter_100ms_block == 9)
        taskman_tempRH = 2;  // read and process temp & humidity measurement
      break;
  }
}

uint32_t taskman_timeStamp = 0;
uint8_t taskman_didSomeTasks = 0;

// execute necessary tasks while we're awake and have things to do
void taskManager(void) {
  // just for capturing start time of taskmanager loop
  if (taskman_buttons && DEBUG_MAIN_LOOP) {
    taskman_timeStamp = micros();
    taskman_didSomeTasks = 1;
  }

  // Do Baro first, because the ADC prep & read cycle is time dependent (must have >9ms between prep
  // & read).  If other tasks delay the start of the baro prep step by >1ms, then next cycle when we
  // read ADC, the Baro won't be ready.
  if (taskman_baro) {
    baro.update();
    taskman_baro = 0;
  }
  if (taskman_buttons) {
    buttons_update();
    taskman_buttons = 0;
  }
  if (taskman_speakerTimer) {
    onSpeakerTimer();
    taskman_speakerTimer = 0;
  }
  if (taskman_estimateWind) {
    estimateWind();
    taskman_estimateWind = 0;
  }
  if (taskman_imu) {
    imu_update();
    taskman_imu = 0;
  }
  if (taskman_gps) {
    gps_update();
    taskman_gps = 0;
  }
  if (taskman_power) {
    power_update();
    taskman_power = 0;
  }
  if (taskman_log) {
    log_update();
    taskman_log = 0;
  }
  if (taskman_display) {
    display_update();
    taskman_display = 0;
  }
  if (taskman_tempRH) {
    tempRH_update(taskman_tempRH);
    taskman_tempRH = 0;
  }
  if (taskman_SDCard) {
    SDcard_update();
    taskman_SDCard = 0;
  }
#ifdef MEMORY_PROFILING
  if (taskman_memory_stats) {
    printMemoryUsage();
    taskman_memory_stats = 0;
  }
#endif

  if (taskman_didSomeTasks && DEBUG_MAIN_LOOP) {
    taskman_didSomeTasks = 0;
    taskman_timeStamp = micros() - taskman_timeStamp;
    Serial.print("10ms: ");
    Serial.print((uint8_t)counter_10ms_block);
    Serial.print(" 100ms: ");
    Serial.print((uint8_t)counter_100ms_block);
    Serial.print(" taskTime: ");
    Serial.println(taskman_timeStamp);
  }
}
