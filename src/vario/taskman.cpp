#include "taskman.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <atomic>
#include "task.h"

#include "comms/ble.h"
#include "comms/factory_discovery.h"
#include "comms/fanet_radio.h"
#include "comms/leaf_log_sync.h"
#include "comms/udp_message_server.h"
#include "comms/webserver.h"
#include "diagnostics/cpu_utilization.h"
#include "diagnostics/diagnostic_network/diagnostic_network.h"
#include "diagnostics/heap_monitor.h"
#include "diagnostics/self_test/selfTest.h"
#include "hardware/Leaf_SPI.h"
#include "hardware/aht20.h"
#include "hardware/buttons.h"
#include "hardware/lc86g.h"
#include "hardware/motion_sensor.h"
#include "hardware/ms5611.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logbook/logbook_store.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "navigation/thermal_core.h"
#include "navigation/thermal_tracker.h"
#include "navigation/user_waypoints.h"
#include "power.h"
#include "storage/sd_card.h"
#include "system/usb_state.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "utils/magic_enum.h"
#include "wind_estimate/wind_estimate.h"

// Bit of a hack for now as we don't have a good way to pass big chunks
// of data around, so, we do it on the stack.  Default is 8KB
SET_LOOP_TASK_STACK_SIZE(12 * 1024);  // 12KB

#define DEBUG_MAIN_LOOP false

namespace {
  std::atomic<bool> timerISRsSetup{false};

  const uint32_t TASK_TIMER_FREQ = 1000;     // run at 1000Hz
  const uint64_t TASK_TIMER_PERIOD_MS = 10;  // trigger the ISR every 10ms
  // Set by interrupt at the start of the next task time block
  // A sequence preserves coalesced timer ticks without changing the one-dispatch policy.
  std::atomic<uint32_t> taskTimerTicks{0};
  uint32_t consumedTaskTick = 0;
  uint64_t taskTimerEpochUs = 0;
  void IRAM_ATTR onTaskTimer() { taskTimerTicks.fetch_add(1, std::memory_order_release); }

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

  constexpr uint32_t MEMORY_HEARTBEAT_MS = 5000;
  uint32_t lastMemoryHeartbeatMs = 0;
  bool loggedFirstBaroTask = false;
  bool loggedFirstButtonsTask = false;
  bool loggedFirstSpeakerTask = false;
  bool loggedFirstWindTask = false;
  bool loggedFirstImuTask = false;
  bool loggedFirstGpsTask = false;
  bool loggedFirstPowerTask = false;
  bool loggedFirstLogTask = false;
  bool loggedFirstDisplayTask = false;
  bool loggedFirstDisplayAfterTask = false;
  bool loggedFirstTempRhTask = false;
  bool loggedFirstSdCardTask = false;
  bool loggedFirstSelfTestTask = false;
  bool loggedFirstDiagnosticNetwork = false;
  bool loggedFirstFactoryDiscovery = false;
  uint32_t lastChargingButtonPollMs = 0;

  void checkpointOnce(bool& logged, const char* event) {
    if (logged) return;
    logged = true;
    heap_monitor::checkpoint(event);
  }
}  // namespace

/////////////////////////////////////////////////
//             SETUP              ///////////////
/////////////////////////////////////////////////
void TaskManager::init() {
  // Adjust the priority of this (the loop event)'s FreeRTOS priority
  vTaskPrioritySet(NULL, 10);

  // turn on and handle all device initialization
  heap_monitor::checkpoint("taskman-power-start");
  power.bootUp();
#ifdef FANET_CAPABLE
  fanetRadio.logDetectionResult();
#endif
  heap_monitor::checkpoint("taskman-power-end");

  if (!timerISRsSetup.exchange(false, std::memory_order_acq_rel)) {
    heap_monitor::checkpoint("taskman-timers-start");
    // Start Main System Timer for Interrupt Events (this will tell Main Loop to set tasks every
    // interrupt cycle)
    taskTimer_ = timerBegin(TASK_TIMER_FREQ);
    // timer, ISR call.  NOTE: timerDetachInterrupt() does the opposite
    timerAttachInterrupt(taskTimer_, &onTaskTimer);
    // auto reload timer ever time we've counted a sample length
    taskTimerEpochUs = cpu_utilization::nowUs();
    timerAlarm(taskTimer_, TASK_TIMER_PERIOD_MS, true, 0);
    cpu_utilization::timerArmed(cpu_utilization::nowUs() - taskTimerEpochUs);

    // Start Charge System Timer for Interrupt Events (this will tell Main Loop to do tasks every
    // interrupt cycle)
    chargeTimer_ = timerBegin(CHARGE_TIMER_FREQ);
    // timer, ISR call.  NOTE: timerDetachInterrupt() does the opposite
    timerAttachInterrupt(chargeTimer_, &onChargeTimer);
    // auto reload timer ever time we've counted a sample length
    timerAlarm(chargeTimer_, CHARGE_TIMER_PERIOD_MS, true, 0);
    heap_monitor::checkpoint("taskman-timers-end");
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
  cpu_utilization::update();
  const bool networkWork = !leafLogSync.transferInFlight() && sdcard.firmwareOwnsMassStorage() &&
                           (WiFi.status() == WL_CONNECTED || webserver_user_app_active());
  const bool heartbeatDue = millis() - lastMemoryHeartbeatMs >= MEMORY_HEARTBEAT_MS;
  {
    cpu_utilization::Scope services(
        runtime_capture::Work::Services, true,
        networkWork || heartbeatDue ||
            (cpu_utilization::capturing() && settings.udpInjectionEnabled()));
    udpMessageServer.update();
    if (!leafLogSync.scanning() && !leafLogSync.transferInFlight()) LogbookStore::serviceRepair();

    // LOOP NOTES:
    // when re-entering PowerState::On, be sure to start from tasks #1, so baro ADC can be
    // re-prepped before reading

    if (networkWork) {
      webserver_setup();
      webserver_loop();
      factoryDiscovery.update();
    }

    if (heartbeatDue) {
      lastMemoryHeartbeatMs = millis();
      heap_monitor::checkpoint("main-heartbeat");
    }
  }

  const auto& info = power.info();
  if (info.onState == PowerState::On)
    updateWhileOn();
  else if (info.onState == PowerState::OffUSB) {
    // Charging uses a different cadence; finish the 10ms capture at the mode boundary.
    cpu_utilization::freeze();
    updateWhileCharging();
  } else
    fatalError("Unsupported onState '%s' (%u) in TaskManager::update()",
               nameOf(info.onState).c_str(), info.onState);
}

void TaskManager::updateWhileCharging() {
  // A tap/second tap must be observable while an upload worker is running. Keep
  // debounce and hold timing independent of the 500ms display/charging cadence.
  if (millis() - lastChargingButtonPollMs >= 5) {
    lastChargingButtonPollMs = millis();
    buttons.update();
  }
  if (buttons.inspectPins() != Button::NONE) goToSleep = false;
  // Scanning yields after a short work slice rather than waiting for the 500ms display timer.
  if (leafLogSync.scanning()) leafLogSync.update();

  if (nextChargeTimerBlock.exchange(false, std::memory_order_acq_rel)) {
    leafLogSync.update();
    if (leafLogSync.takePowerOnReady()) {
      display.clear();
      display.showOnSplash();
      display.setPage((MainPage)settings.startPage);
      speaker.playSound(fx::enter);
      if (!power.switchToOnState()) {
        display.setPage(MainPage::Charging);
        display.update();
      }
      return;
    }
    // Display Charging Page
    display.setPage(MainPage::Charging);
    display.update();  // update display based on battery charge state etc

    // Check SD Card State and remount if card was inserted
    // An upload owns an open SD file on its worker. Defer mount/unmount changes
    // until it has closed the file and published its result.
    if (!leafLogSync.transferInFlight()) sdcard.update();

    // update battery level and charge state
    power.readBatteryState();

    // Try to connect to the diagnostic network once while in charge-only mode. If it connects,
    // the connection will be preserved when the device switches to the on state.
    if (!leafLogSync.transferInFlight()) diagnostic_network.update();

    // Check Buttons
    buttons.update();  // also poll on a charging tick immediately after waking

    // Prep to end this cycle and sleep
    if (buttons.inspectPins() == Button::NONE && diagnostic_network.canSleepWhileCharging() &&
        leafLogSync.canSleepWhileCharging() && !leaf_usb::shouldStayAwakeForHost()) {
      goToSleep = true;  // get ready to sleep if no button is being pushed
    } else {
      goToSleep = false;
    }
  } else {
    if (goToSleep && settings.system_ecoMode) {  // don't allow sleep if ECO_MODE is off

      goToSleep = false;  // we don't want to sleep again as soon as we wake up; we want to wait
                          // until we've done 'doTasks' before sleeping again

      // Wake up if any of the five active-high buttons is pressed.
      buttons.enableSleepWakeFromAnyButton();

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
  bool startedTaskBlock = false;
  uint8_t taskBlockButtonMask = 0;

  // Check flag set by timer interrupt
  const uint32_t tick = taskTimerTicks.load(std::memory_order_acquire);
  if (tick != consumedTaskTick) {
    consumedTaskTick = tick;
    startedTaskBlock = true;
    // setNecessaryTasksForBlock advances the logical index, including one direct baro call.
    if (cpu_utilization::capturing()) {
      const uint64_t actualStartUs = cpu_utilization::nowUs();
      const uint8_t nextIndex = (current100msBlock * 10 + current10msBlock + 1) % 100;
      cpu_utilization::beginSlot(nextIndex, tick, taskTimerEpochUs + uint64_t(tick) * 10000,
                                 actualStartUs);
      taskBlockButtonMask = cpu_utilization::buttonPinMask(buttons.inspectPins());
      display.clearLastRenderContext();
    }
    // We only do this once every 10ms
    cpu_utilization::Scope tasks(runtime_capture::Work::Tasks);
    setNecessaryTasksForBlock();
  }

  {
    const bool diagnosticWork =
        diagnostic_network.state() != DiagnosticNetwork::State::NoNetworkFound &&
        diagnostic_network.state() != DiagnosticNetwork::State::Error;
    cpu_utilization::Scope work(
        startedTaskBlock ? runtime_capture::Work::Tasks : runtime_capture::Work::Services, true,
        startedTaskBlock || diagnosticWork);
    doNecessaryTasks();
  }

  if (startedTaskBlock && cpu_utilization::capturing()) {
    taskBlockButtonMask |= cpu_utilization::buttonPinMask(buttons.inspectPins());
    cpu_utilization::tasksFinished(taskBlockButtonMask,
                                   static_cast<uint8_t>(display.lastRenderContext()));
  }

  // GPS Serial Buffer Read
  // stop reading if no more data is available, OR, if our 10ms time block is up (because main timer
  // interrupt advanced taskTimerTicks). readLine also checks between individual characters;
  // sentence-completion callbacks remain inline and may finish after the tick.
  bool gpsHasData = true;
  bool sampleEmptyPoll = startedTaskBlock;
  while (gpsHasData && taskTimerTicks.load(std::memory_order_acquire) == consumedTaskTick) {
    gpsHasData = lc86g.readLine(taskTimerTicks, consumedTaskTick, sampleEmptyPoll);
    sampleEmptyPoll = false;
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
  if (selfTest.updateNeeded())
    performTask.selfTest = true;
  else
    (performTask.selfTest = false);

  // set additional tasks to complete, broken down into 10ms block cycles.  (embedded if()
  // statements allow for tasks every second, spaced out on different 100ms blocks)
  switch (current10msBlock) {
    case 0:
      cpu_utilization::baroStarting();
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
      cpu_utilization::baroStarting();
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
      if (current100msBlock == 1) {
        performTask.thermalDetector = true;    // every second: thermal detector
        performTask.thermalNavigation = true;  // every half second: thermal nav/display cache
      }
      if (current100msBlock == 2) performTask.log = true;  // every second: logging
      if (current100msBlock == 3 || current100msBlock == 8)
        performTask.display = 1;  // Update LCD every half-second on the 3rd and 8th 100ms blocks
      if (current100msBlock == 4)
        performTask.tempRH = true;  // trigger the start of a new temp & humidity measurement
      // 5 - gps
      if (current100msBlock == 6)
        performTask.thermalNavigation = true;  // every half second: thermal nav/display cache
      if (current100msBlock == 7) {
        performTask.power = true;   // every second: power checks
        performTask.sdCard = true;  // check if SD card state has changed and remount if needed
      }
      // 8 - LCD
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
    cpu_utilization::baroStarting();
    ms5611.update();
    checkpointOnce(loggedFirstBaroTask, "task-baro-first");
    performTask.baro = false;
  }
  if (performTask.buttons) {
    buttons.update();
    checkpointOnce(loggedFirstButtonsTask, "task-buttons-first");
    performTask.buttons = false;
  }
  if (performTask.speakerTimer) {
    speaker.update();
    checkpointOnce(loggedFirstSpeakerTask, "task-speaker-first");
    performTask.speakerTimer = false;
  }
  if (performTask.estimateWind) {
    windEstimator.estimateWind();
    checkpointOnce(loggedFirstWindTask, "task-wind-first");
    performTask.estimateWind = false;
  }
  if (performTask.imu) {
    MotionSensor::getInstance().update();
    if (!imu.velocityValid()) baro.invalidateClimb();
    checkpointOnce(loggedFirstImuTask, "task-imu-first");
    performTask.imu = false;
  }
  if (performTask.gps) {
    gps.update();
    checkpointOnce(loggedFirstGpsTask, "task-gps-first");
    performTask.gps = false;
  }
  if (performTask.thermalDetector) {
    if (settings.disp_showThermalCorePage || settings.disp_showThermalTrackPage)
      thermalTracker.updateDetector();
    performTask.thermalDetector = false;
  }
  if (performTask.thermalNavigation) {
    if (settings.disp_showThermalTrackPage) thermalTracker.updateNavigation();
    if (settings.disp_showThermalCorePage) thermalCore.update();
    performTask.thermalNavigation = false;
  }
  if (performTask.power) {
    power.update();
    checkpointOnce(loggedFirstPowerTask, "task-power-first");
    performTask.power = false;
  }
  if (performTask.log) {
    log_update();
    checkpointOnce(loggedFirstLogTask, "task-log-first");
    performTask.log = false;
  }
  if (performTask.display) {
    checkpointOnce(loggedFirstDisplayTask, "task-display-before");
    display.update();
    checkpointOnce(loggedFirstDisplayAfterTask, "task-display-after");
    if (!restoredNavAfterFirstDisplayUpdate_) {
      restoredNavAfterFirstDisplayUpdate_ = true;
      if (sdcard.isMounted()) {
        heap_monitor::checkpoint("task-nav-restore-start");
        navigator.loadPersistedState(false);
        heap_monitor::checkpoint("task-nav-restore-state");
        user_waypoints::loadIntoNavigator();
        heap_monitor::checkpoint("task-nav-restore-end");
      }
    }
    performTask.display = false;
  }
  if (performTask.tempRH) {
    aht20.update();
    checkpointOnce(loggedFirstTempRhTask, "task-temp-rh-first");
    performTask.tempRH = false;
  }
  if (performTask.sdCard) {
    sdcard.update();
    checkpointOnce(loggedFirstSdCardTask, "task-sdcard-first");
    performTask.sdCard = false;
  }
  if (performTask.selfTest) {
    selfTest.update();
    checkpointOnce(loggedFirstSelfTestTask, "task-self-test-first");
    performTask.selfTest = false;
  }

  diagnostic_network.update();
  checkpointOnce(loggedFirstDiagnosticNetwork, "task-diag-net-first");
  factoryDiscovery.update();
  checkpointOnce(loggedFirstFactoryDiscovery, "task-factory-disc-first");

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
