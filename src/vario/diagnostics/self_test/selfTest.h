#pragma once

// Self Test functions for Vario device
#include <Arduino.h>
#include "selfTest_displayScreens.h"

class SelfTest {
 public:
  enum class Status { Pass, Fail, Running, Complete, Unknown };

  Status status = Status::Unknown;
  Status statusAutoTests = Status::Unknown;
  Status statusInteractiveTests = Status::Unknown;

  // Run all self tests
  Status runAllTests();
  Status runAutoTests(bool closeFileWhenDone);  // allow closing file in this method if you only
                                                // want to run Auto Tests
  Status runInteractiveTests(bool closeFileWhenDone);  // allow closing file in this method if you
                                                       // only want to run Interactive Tests
  void closeTestFile();
  void clearResults();

  // Individual automated test functions
  static Status testBaro();
  static Status testIMU();
  static Status testGPS();
  static Status testAmbient();
  static Status testDisplay();
  static Status testSDCard();
  static Status testPower();
  static Status testSpeaker();

  // results for all self tests, including both automated and interactive
  struct Results {
    Status sdCard = Status::Unknown;
    Status baro = Status::Unknown;
    Status imu = Status::Unknown;
    Status gps = Status::Unknown;
    Status ambient = Status::Unknown;
    Status display = Status::Unknown;
    Status buttons = Status::Unknown;
    Status power = Status::Unknown;
    Status speaker = Status::Unknown;
    Status vario = Status::Unknown;

    void reset() { *this = Results{}; }  // reset all back to Unknown
  } results;
};
extern SelfTest selfTest;

// Interactive Test that requires user action/input and specific display updates for giving
// instructions
class InteractiveTest {
 public:
  virtual SelfTest::Status update();  // returns true if update() needs to be called again
  SelfTest::Status status = SelfTest::Status::Unknown;

 protected:
  int16_t waitForInput = 800;  // 10ms ticks to wait for user input
};

class ButtonsInteractiveTest : public InteractiveTest {
 public:
  SelfTest::Status update();

 protected:
  bool upPressed = false;
  bool downPressed = false;
  bool leftPressed = false;
  bool rightPressed = false;
  bool centerPressed = false;

  SelfTest_PageButtons selfTest_pageButtons{&upPressed, &downPressed, &leftPressed, &rightPressed,
                                            &centerPressed};  //. button test display
};

class VarioInteractiveTest : public InteractiveTest {
 public:
  SelfTest::Status update();

 protected:
  float initialAltitude = 0.0f;
  float maxAltitude = 0.0f;
  float deltaAltitude = 0.0f;
  float climb = 0.0f;
  float maxClimb = 0.0f;
  float maxSink = 0.0f;
  bool delayForCalibration = false;
  bool initializedTest = false;

  SelfTest_PageVario selfTest_pageVario{
      &deltaAltitude, &maxAltitude, &climb,
      &maxClimb,      &maxSink,     &delayForCalibration};  //. vario test display
};