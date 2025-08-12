#pragma once

#include <Arduino.h>
#include <cstdint>

#include "dispatch/pollable.h"

// List of tasks managed by TaskManager and whether they are necessary in a particular work block.
// Default to tasks being needed, so they execute upon startup.
struct ManagedTasks {
  bool buttons = true;       // poll & process buttons
  bool speakerTimer = true;  // adjust speaker notes
  bool baro = true;         // (1) preprocess on-chip ADC pressure, (2) read pressure and preprocess
                            // on-chip ADC temperature, (3) read temp and calulate true Alt, (4)
                            // filter Alt, update climb, and store values etc
  bool imu = true;          // read sensors and update values
  bool gps = true;          // process any NMEA strings and update values
  bool display = true;      // update display
  bool power = true;        // check battery, check auto-turn-off, etc
  bool log = true;          // check auto-start, increment timers, update log file, etc
  bool tempRH = true;       // (1) trigger temp & humidity measurements, (2) process values and save
  bool sdCard = true;       // check if SD card state has changed and attempt remount if needed
  bool memoryStats = true;  // Prints memory usage reports
  bool estimateWind = true;  // estimate wind speed and direction
};

// This is where the bulk of the task management work happens.  We are slowly moving away from this
// into a FreeRTOS task based scheduler, so, we expect this content to shrink over time
class TaskManager : IPollable {
 public:
  void init();

  // IPollable
  void update();

 private:
  void updateWhileOn();
  void setNecessaryTasksForBlock();  // set necessary tasks for 10ms block
  void doNecessaryTasks();

  void updateWhileCharging();

  bool goToSleep = false;

  uint32_t tNecessaryTasksStart = 0;
  bool performedNecessaryTasks = false;

  // Opportunities to perform necessary work occur every 10ms with a repeating pattern every 1000ms.
  // Tasks can be targeted to execute at certain combinations of 10ms and 100ms indices within the
  // 1-second pattern.

  // Varies between 0 and 9, then wraps back to 0 as the 100ms block increments
  uint8_t current10msBlock = 0;

  // Varies between 0 and 9, then wraps back to 0 as the 1-second pattern repeats
  uint8_t current100msBlock = 0;

  ManagedTasks performTask;

  // Main system event/task timer
  hw_timer_t* taskTimer_ = nullptr;

  // Standby system timer (when on USB power and charging battery, but otherwise "off" (i.e., soft
  // off))
  hw_timer_t* chargeTimer_ = nullptr;
};
