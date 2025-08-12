#include "taskman.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <atomic>
#include "task.h"

#include "comms/ble.h"
#include "hardware/Leaf_SPI.h"
#include "hardware/aht20.h"
#include "hardware/icm_20948.h"
#include "hardware/lc86g.h"
#include "hardware/ms5611.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

#ifdef MEMORY_PROFILING
#include "diagnostics/memory_report.h"
#endif

#ifdef DEBUG_WIFI
#include <WiFi.h>
#include "comms/debug_webserver.h"
#include "comms/udp_message_server.h"
#endif

// Bit of a hack for now as we don't have a good way to pass big chunks
// of data around, so, we do it on the stack.  Default is 8KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);  // 16KB

#define DEBUG_MAIN_LOOP false

namespace {
  std::atomic<bool> timerISRsSetup{false};

  const uint32_t TASK_TIMER_FREQ = 1000;     // run at 1000Hz
  const uint64_t TASK_TIMER_PERIOD_MS = 10;  // trigger the ISR every 10ms
  // Set by interrupt at the start of the next task time block
  std::atomic<bool> nextTaskTimerBlock{false};
  void IRAM_ATTR onTaskTimer() { nextTaskTimerBlock.store(true, std::memory_order_release); }

  const uint32_t CHARGE_TIMER_FREQ = 1000;      // run at 1000Hz
  const uint64_t CHARGE_TIMER_PERIOD_MS = 500;  // trigger the ISR every 500ms
  // Set by interrupt at the start of the next charge time block
  std::atomic<bool> nextChargeTimerBlock{false};
  // Lowest 32 bits of micros() at which most recent charge timer block started
  std::atomic<uint32_t> tChargeTimerBlock{0};
  void IRAM_ATTR onChargeTimer() {
    tChargeTimerBlock.store(static_cast<uint32_t>(micros()), std::memory_order_release);
    nextChargeTimerBlock.store(true, std::memory_order_release);
  }
}  // namespace

/////////////////////////////////////////////////
//             SETUP              ///////////////
/////////////////////////////////////////////////
void TaskManager::init() {
#ifdef DEBUG_WIFI
  // Start WiFi
  WiFi.begin();
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.println("WiFi Event " + WiFi.localIP().toString() + ": " + event);
  });

  // Start WebServer
  webserver_setup();

  // Start UDP message server
  udpMessageServer.init();
#endif

  // Adjust the priority of this (the loop event)'s FreeRTOS priority
  vTaskPrioritySet(NULL, 10);

  // turn on and handle all device initialization
  power.bootUp();

  if (!timerISRsSetup.exchange(false, std::memory_order_acq_rel)) {
    // Start Main System Timer for Interrupt Events (this will tell Main Loop to set tasks every
    // interrupt cycle)
    taskTimer_ = timerBegin(TASK_TIMER_FREQ);
    // timer, ISR call.  NOTE: timerDetachInterrupt() does the opposite
    timerAttachInterrupt(taskTimer_, &onTaskTimer);
    // auto reload timer ever time we've counted a sample length
    timerAlarm(taskTimer_, TASK_TIMER_PERIOD_MS, true, 0);

    // Start Charge System Timer for Interrupt Events (this will tell Main Loop to do tasks every
    // interrupt cycle)
    chargeTimer_ = timerBegin(CHARGE_TIMER_FREQ);
    // timer, ISR call.  NOTE: timerDetachInterrupt() does the opposite
    timerAttachInterrupt(chargeTimer_, &onChargeTimer);
    // auto reload timer ever time we've counted a sample length
    timerAlarm(chargeTimer_, CHARGE_TIMER_PERIOD_MS, true, 0);
  } else {
    fatalError("Attempted to set up singleton task timers when they were already set up");
  }

  // All done!
  Serial.println("Finished Setup");
}

/////////////////////////////////////////////////
// Main Loop for Processing Tasks ///////////////
/////////////////////////////////////////////////
/*
The main loop prioritizes processesing any data in the serial buffer so as not to miss any NMEA GPS
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
void TaskManager::update() {
  // LOOP NOTES:
  // when re-entering PowerState::On, be sure to start from tasks #1, so baro ADC can be re-prepped
  // before reading

#ifdef DEBUG_WIFI
  webserver_loop();
#endif

  const auto& info = power.info();
  if (info.onState == PowerState::On)
    updateWhileOn();
  else if (info.onState == PowerState::OffUSB)
    updateWhileCharging();
  else
    fatalError("Unsupported onState '%s' (%d) in TaskManager::update()", nameOf(info.onState),
               info.onState);
}

void TaskManager::updateWhileCharging() {
  if (nextChargeTimerBlock.exchange(false, std::memory_order_acq_rel)) {
    // Display Charging Page
    display.setPage(MainPage::Charging);
    display.update();  // update display based on battery charge state etc

    // Check SD Card State and remount if card was inserted
    sdcard.update();

    // update battery level and charge state
    power.readBatteryState();

    // Check Buttons
    auto buttonPushed =
        buttons.update();  // check Button for any presses (user can turn ON from charging state)

    // Prep to end this cycle and sleep
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
      uint32_t microsNow = static_cast<uint32_t>(micros());
      uint32_t sleepTimeStamp = tChargeTimerBlock.load(std::memory_order_acquire);
      uint32_t sleepMicros =
          sleepTimeStamp + (1000 * (CHARGE_TIMER_PERIOD_MS - 1)) -
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

void TaskManager::updateWhileOn() {
  // Check flag set by timer interrupt
  if (nextTaskTimerBlock.exchange(false, std::memory_order_acq_rel)) {
    // We only do this once every 10ms
    setNecessaryTasksForBlock();
  }

  doNecessaryTasks();

  // GPS Serial Buffer Read
  // stop reading if no more data is available, OR, if our 10ms time block is up (because main timer
  // interrupt fired and set setTasks to true)
  bool gpsHasData = true;
  while (gpsHasData && !nextTaskTimerBlock.load(std::memory_order_acquire)) {
    gpsHasData = lc86g.readData();
  }
}

void TaskManager::setNecessaryTasksForBlock() {
  // increment time counters
  if (++current10msBlock >= 10) {
    current10msBlock = 0;  // every 10 periods of 10ms, go back to 0 (100ms total)
    if (++current100msBlock >= 10) {
      current100msBlock = 0;  // every 10 periods of 100ms, go back to 0 (1sec total)
    }
  }

  // tasks to complete every 10ms
  performTask.buttons = true;
  performTask.baro = true;
  performTask.speakerTimer = true;

  // set additional tasks to complete, broken down into 10ms block cycles.  (embedded if()
  // statements allow for tasks every second, spaced out on different 100ms blocks)
  switch (current10msBlock) {
    case 0:
      ms5611.update();  // begin updating MS5611 every 50ms on the 0th and 5th blocks
      break;
    case 1:
      break;
    case 2:
      performTask.imu = true;           // update accel every 50ms during the 2nd & 7th blocks
      performTask.estimateWind = true;  // estimate wind speed and direction
      break;
    case 3:
      break;
    case 4:
      break;
    case 5:
      ms5611.update();  // begin updating MS5611 every 50ms on the 0th and 5th blocks
      break;
    case 6:
      break;
    case 7:
      performTask.imu = true;  // update accel every 50ms during the 2nd & 7th blocks
      break;
    case 8:
      break;
    case 9:

      // Tasks every second complete here in the 9th 10ms block.  Pick a unique 100ms block for each
      // task to space things out

      if (current100msBlock == 0 || current100msBlock == 5)
        performTask.gps =
            true;  // gps update every half second (this avoids any aliasing issues if we
                   // keep trying to update GPS in the middle of an NMEA sentence string)
      if (current100msBlock == 1) performTask.power = true;  // every second: power checks
      if (current100msBlock == 2) performTask.log = true;    // every second: logging
      if (current100msBlock == 3 || current100msBlock == 8)
        performTask.display = 1;  // Update LCD every half-second on the 3rd and 8th 100ms blocks
      if (current100msBlock == 4)
        performTask.tempRH = true;  // trigger the start of a new temp & humidity measurement
      // 5 - gps
      if (current100msBlock == 6)
        performTask.sdCard = 1;  // check if SD card state has changed and remount if needed
// 7 - available
// 8 - LCD
#ifdef MEMORY_PROFILING
      if (current100msBlock == 7) {
        performTask.memoryStats = 1;
      }
#endif
      if (current100msBlock == 9)
        performTask.tempRH = true;  // read and process temp & humidity measurement
      break;
  }
}

// execute necessary tasks while we're awake and have things to do
void TaskManager::doNecessaryTasks(void) {
  // just for capturing start time of taskmanager loop
  if (performTask.buttons && DEBUG_MAIN_LOOP) {
    tNecessaryTasksStart = micros();
    performedNecessaryTasks = true;
  }

  // Do MS5611 first, because the ADC prep & read cycle is time dependent (must have >9ms between
  // prep & read).  If other tasks delay the start of the MS5611 prep step by >1ms, then next cycle
  // when we read ADC, the MS5611 won't be ready.
  if (performTask.baro) {
    ms5611.update();
    baro.update();
    performTask.baro = false;
  }
  if (performTask.buttons) {
    buttons.update();
    performTask.buttons = false;
  }
  if (performTask.speakerTimer) {
    speaker.update();
    performTask.speakerTimer = false;
  }
  if (performTask.estimateWind) {
    estimateWind();
    performTask.estimateWind = false;
  }
  if (performTask.imu) {
    ICM20948::getInstance().update();
    performTask.imu = false;
  }
  if (performTask.gps) {
    gps.update();
    performTask.gps = false;
  }
  if (performTask.power) {
    power.update();
    performTask.power = false;
  }
  if (performTask.log) {
    log_update();
    performTask.log = false;
  }
  if (performTask.display) {
    display.update();
    performTask.display = false;
  }
  if (performTask.tempRH) {
    AHT20::getInstance().update();
    performTask.tempRH = false;
  }
  if (performTask.sdCard) {
    sdcard.update();
    performTask.sdCard = false;
  }
#ifdef MEMORY_PROFILING
  if (performTask.memoryStats) {
    printMemoryUsage();
    performTask.memoryStats = false;
  }
#endif

  if (performedNecessaryTasks && DEBUG_MAIN_LOOP) {
    performedNecessaryTasks = false;
    uint64_t dt = micros() - tNecessaryTasksStart;
    Serial.print("10ms: ");
    Serial.print((uint8_t)current10msBlock);
    Serial.print(" 100ms: ");
    Serial.print((uint8_t)current100msBlock);
    Serial.print(" taskTime: ");
    Serial.println(dt);
  }
}
