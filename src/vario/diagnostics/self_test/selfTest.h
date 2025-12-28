#pragma once

// Self Test functions for Vario device
#include <Arduino.h>
#include "selfTest_displayScreens.h"

class SelfTest {
 public:
  enum class Status { Unknown, Fail, Pass };

  // Run all self tests
  static void runAllTests();
  static void runAutoTests(bool closeFileWhenDone);
  static void runInteractiveTests(bool closeFileWhenDone);
  static void clearResults();

  bool running = false;

  // Individual test functions
  static Status testBaro();
  static Status testIMU();
  static Status testGPS();
  static Status testAmbient();
  static Status testDisplay();
  static Status testButtons();
  static Status testSDCard();
  static Status testPower();
  static Status testSpeaker();
  static Status testKalman();

  void selfTest_drawDisplay();  // show progress & results on LCD display

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
    Status kalman = Status::Unknown;
  } results;
};
extern SelfTest selfTest;

// Object for Interactive Test that requires user action/input and specific display updates for
// giving instructions
class InteractiveTest {
 public:
  virtual bool update();  // returns true if test is complete
  SelfTest::Status result = SelfTest::Status::Unknown;
  bool running = false;

 protected:
  int16_t waitForInput = 800;  // 10ms ticks to wait for user input
};

class ButtonsInteractiveTest : public InteractiveTest {
 public:
  bool update();

 protected:
  bool upPressed = false;
  bool downPressed = false;
  bool leftPressed = false;
  bool rightPressed = false;
  bool centerPressed = false;

  SelfTest_PageButtons selfTest_pageButtons{&upPressed, &downPressed, &leftPressed, &rightPressed,
                                            &centerPressed};  //. button test display
};
