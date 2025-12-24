#pragma once

// Self Test functions for Vario device
#include <Arduino.h>

class SelfTest {
 public:
  // Run all self tests
  static void runAllTests();
  static void runAutoTests();
  static void runInteractiveTests();

  // Individual test functions
  static bool testBaro();
  static bool testIMU();
  static bool testGPS();
  static bool testAmbient();
  static bool testDisplay();
  static bool testButtons();
  static bool testSDCard();
  static bool testPower();
  static bool testSpeaker();

  void selfTest_drawDisplay();  // show progress & results on LCD display

  struct Results {
    bool baro;
    bool imu;
    bool gps;
    bool ambient;
    bool display;
    bool buttons;
    bool sdCard;
    bool power;
    bool speaker;
  } results;
};
extern SelfTest selfTest;