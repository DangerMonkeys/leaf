# 1 "C:\\Users\\oxothnk\\AppData\\Local\\Temp\\tmp8gwj7g7t"
#include <Arduino.h>
# 1 "G:/My Drive/Projects/leaf/src/vario/vario.ino"


#include <Arduino.h>
#include <HardwareSerial.h>

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



SET_LOOP_TASK_STACK_SIZE(16 * 1024);

#define DEBUG_MAIN_LOOP false


#define AVAIL_GPIO_0 0
#define AVAIL_GPIO_21 21
#define AVAIL_GPIO_39 39


hw_timer_t* task_timer = NULL;
#define TASK_TIMER_FREQ 1000
#define TASK_TIMER_LENGTH 10
void onTaskTimer(void);



hw_timer_t* charge_timer = NULL;
#define CHARGE_TIMER_FREQ 1000
#define CHARGE_TIMER_LENGTH 500
void onChargeTimer(void);
char chargeman_doTasks =
    0;
uint64_t sleepTimeStamp =
    0;


char counter_10ms_block = 0;
char counter_100ms_block = 0;


bool gps_is_quiet = 0;


char taskman_setTasks = 1;

char taskman_buttons = 1;
char taskman_baro = 1;


char taskman_imu = 1;
char taskman_gps = 1;
char taskman_display = 1;
char taskman_power = 1;
char taskman_log = 1;
char taskman_tempRH = 1;
char taskman_SDCard = 1;
char taskman_memory_stats = 1;
char taskman_estimateWind = 1;
char taskman_ble = 1;



uint8_t display_do_tracker = 1;
#define TESTING_LOOP false
void setup();
void IRAM_ATTR onTaskTimer();
void IRAM_ATTR onChargeTimer();
void loop();
void main_CHARGE_loop();
void main_ON_loop();
void setTasks(void);
void taskManager(void);
#line 91 "G:/My Drive/Projects/leaf/src/vario/vario.ino"
void setup() {

  Serial.begin(115200);
  delay(200);
  Serial.println("Starting Setup");

#ifdef DEBUG_WIFI

  WiFi.begin();
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.println("WiFi Event " + WiFi.localIP().toString() + ": " + event);
  });
#endif
#ifdef DEBUG_WIFI

  webserver_setup();
#endif


  vTaskPrioritySet(NULL, 10);


  power_bootUp();



  task_timer = timerBegin(TASK_TIMER_FREQ);
  timerAttachInterrupt(
      task_timer,
      &onTaskTimer);
  timerAlarm(task_timer,
             TASK_TIMER_LENGTH,
             true,
             0);



  charge_timer = timerBegin(CHARGE_TIMER_FREQ);
  timerAttachInterrupt(
      charge_timer,
      &onChargeTimer);
  timerAlarm(charge_timer,
             CHARGE_TIMER_LENGTH,
             true,
             0);


  if (BLUETOOTH_ON) {
    BLE::get().setup();
  }


  Serial.println("Finished Setup");



}


void IRAM_ATTR onTaskTimer() {

  taskman_setTasks = 1;

}


void IRAM_ATTR onChargeTimer() {
  sleepTimeStamp = micros();

  chargeman_doTasks = 1;

}
# 186 "G:/My Drive/Projects/leaf/src/vario/vario.ino"
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
# 215 "G:/My Drive/Projects/leaf/src/vario/vario.ino"
    display_setPage(page_charging);
    display_update();


    SDcard_update();


    power_readBatteryState();


    auto buttonPushed =
        buttons_update();


    chargeman_doTasks = 0;
    if (buttonPushed == Button::NONE)
      goToSleep = true;
  } else {
    if (goToSleep && ECO_MODE) {

      goToSleep = false;



      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_CENTER, HIGH);
      esp_sleep_enable_ext0_wakeup(
          (gpio_num_t)BUTTON_PIN_LEFT,
          HIGH);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_RIGHT, HIGH);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_UP, HIGH);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN_DOWN, HIGH);


      uint64_t microsNow = micros();
      uint64_t sleepMicros =
          sleepTimeStamp + (1000 * (CHARGE_TIMER_LENGTH - 1)) -
          microsNow;
      if (sleepMicros > 496 * 1000)
        sleepMicros = 496 * 1000;

      esp_sleep_enable_timer_wakeup(sleepMicros);


      if (ECO_MODE) {

        esp_light_sleep_start();
      } else {
        Serial.print("microsNow:    ");
        Serial.println(microsNow);
        Serial.print("sleepMicros:  ");
        Serial.println(sleepMicros);
        Serial.print("wakeMicros:   ");
        Serial.println(microsNow + sleepMicros);
        delayMicroseconds(sleepMicros);


      }
    } else {

    }
  }
}


void main_ON_loop() {
  if (taskman_setTasks) {


    setTasks();
    taskman_setTasks = 0;
  }


  taskManager();




  bool gps_buffer_full = true;
  while (gps_buffer_full && !taskman_setTasks) {
    gps_buffer_full = gps_read_buffer_once();
  }







}

bool doBaroTemp = true;

bool baro_startNewCycle =
    false;

void setTasks(void) {

  if (++counter_10ms_block >= 10) {
    counter_10ms_block = 0;
    if (++counter_100ms_block >= 10) {
      counter_100ms_block = 0;
    }
  }


  taskman_buttons = 1;
  taskman_baro = 1;



  switch (counter_10ms_block) {
    case 0:
      baro_startNewCycle = true;

      break;
    case 1:

      break;
    case 2:

      taskman_estimateWind = 1;
      break;
    case 3:

      break;
    case 4:
      taskman_imu = 1;
      break;
    case 5:
      baro_startNewCycle = true;

      break;
    case 6:

      break;
    case 7:
      taskman_ble = 1;

      break;
    case 8:

      break;
    case 9:




      if (counter_100ms_block == 0 || counter_100ms_block == 5)
        taskman_gps = 1;

      if (counter_100ms_block == 1) taskman_power = 1;
      if (counter_100ms_block == 2) taskman_log = 1;
      if (counter_100ms_block == 3 || counter_100ms_block == 8)
        taskman_display = 1;
      if (counter_100ms_block == 4)
        taskman_tempRH = 1;

      if (counter_100ms_block == 6)
        taskman_SDCard = 1;


#ifdef MEMORY_PROFILING
      if (counter_100ms_block == 7) {
        taskman_memory_stats = 1;
      }
#endif
      if (counter_100ms_block == 9)
        taskman_tempRH = 2;
      break;
  }
}

uint32_t taskman_timeStamp = 0;
uint8_t taskman_didSomeTasks = 0;


void taskManager(void) {

  if (taskman_buttons && DEBUG_MAIN_LOOP) {
    taskman_timeStamp = micros();
    taskman_didSomeTasks = 1;
  }




  if (taskman_baro) {
    baro_update(baro_startNewCycle, doBaroTemp);
    taskman_baro = 0;
    baro_startNewCycle = false;
  }
  if (taskman_buttons) {
    buttons_update();
    taskman_buttons = 0;
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
  if (taskman_ble) {
    BLE::get().loop();
    taskman_ble = 0;
  }

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