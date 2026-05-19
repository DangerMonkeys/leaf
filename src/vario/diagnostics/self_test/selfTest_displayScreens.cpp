#include "selfTest_displayScreens.h"

#include "diagnostics/self_test/selfTest.h"
#include "instruments/gps.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"

//////////////////////////////////////////////
// Button Self-Test Page
void SelfTest_PageButtons::show() { push_page(this); }

void SelfTest_PageButtons::draw_extra() {
  uint8_t buttonCluster_x = 48;
  uint8_t buttonCluster_y = 100;
  u8g2.setCursor(23, 40);
  u8g2.setFont(leaf_6x12);
  u8g2.print("Press all");
  u8g2.setCursor(26, 55);
  u8g2.print("Buttons:");
  // Draw button cluster
  // UP
  u8g2.setCursor(buttonCluster_x, buttonCluster_y - 20);
  if (*up_)
    u8g2.print("X");
  else
    u8g2.print("O");
  // DOWN
  u8g2.setCursor(buttonCluster_x, buttonCluster_y + 20);
  if (*down_)
    u8g2.print("X");
  else
    u8g2.print("O");
  // LEFT
  u8g2.setCursor(buttonCluster_x - 20, buttonCluster_y);
  if (*left_)
    u8g2.print("X");
  else
    u8g2.print("O");
  // RIGHT
  u8g2.setCursor(buttonCluster_x + 20, buttonCluster_y);
  if (*right_)
    u8g2.print("X");
  else
    u8g2.print("O");
  // CENTER
  u8g2.setCursor(buttonCluster_x, buttonCluster_y);
  if (*center_)
    u8g2.print("X");
  else
    u8g2.print("O");
}

//////////////////////////////////////////////
// Vario Self-Test Page
void SelfTest_PageVario::show() { push_page(this); }

void SelfTest_PageVario::draw_extra() {
  // delay message
  if (*delayForCalib_) {
    u8g2.setCursor(15, 40);
    u8g2.setFont(leaf_6x12);
    u8g2.print("Please wait");
    u8g2.setCursor(20, 55);
    u8g2.print("for vario");
    u8g2.setCursor(12, 70);
    u8g2.print(" calibration");
    return;
  }

  // instructions to user
  u8g2.setCursor(25, 40);
  u8g2.setFont(leaf_6x12);
  u8g2.print("Quickly");
  u8g2.setCursor(17, 55);
  u8g2.print("raise Leaf");
  u8g2.setCursor(2, 70);
  u8g2.print("above your head");
  u8g2.setCursor(8, 85);
  u8g2.print("and back down");

  // value results
  u8g2.setCursor(10, 110);
  u8g2.setFont(leaf_5x8);
  u8g2.print("Alt Increase: ");
  u8g2.print(*altIncrease_);
  u8g2.setCursor(10, 125);
  u8g2.print("Max Climb:   ");
  u8g2.print(*climbMax_);
  u8g2.setCursor(10, 140);
  u8g2.print("Max Sink:   ");
  u8g2.print(*climbMin_);
}

//////////////////////////////////////////////
// Speaker Self-Test Page
void SelfTest_PageSpeaker::show() { push_page(this); }

void SelfTest_PageSpeaker::draw_extra() {
  // instructions to user
  u8g2.setCursor(17, 40);
  u8g2.setFont(leaf_6x12);
  u8g2.print("Listen for");
  u8g2.setCursor(5, 55);
  u8g2.print("three beeps at");
  u8g2.setCursor(4, 70);
  u8g2.print("low med & high");
  u8g2.setCursor(25, 85);
  u8g2.print("volume");

  u8g2.setCursor(0, 110);
  u8g2.print("YES: UP");
  u8g2.setCursor(0, 125);
  u8g2.print("NO: DOWN");
}

//////////////////////////////////////////////
// GPS Fix Self-Test Page
void SelfTest_PageGPSFix::show() { push_page(this); }

void SelfTest_PageGPSFix::closed(bool removed_from_Stack) {
  if (removed_from_Stack) *cancelled_ = true;
}

void SelfTest_PageGPSFix::draw_extra() {
  const uint32_t remainingSeconds = *remainingSeconds_;
  const uint16_t remainingMinutes = remainingSeconds / 60;
  const uint8_t remainingSecondsPart = remainingSeconds % 60;

  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(7, 31);
  u8g2.print("Place Leaf outside");
  u8g2.setCursor(10, 41);
  u8g2.print("with a clear view");
  u8g2.setCursor(25, 51);
  u8g2.print("of the sky");

  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(0, 67);
  u8g2.print("Timeout:");
  u8g2.setCursor(70, 67);
  u8g2.print(remainingMinutes);
  u8g2.print(":");
  if (remainingSecondsPart < 10) u8g2.print("0");
  u8g2.print(remainingSecondsPart);

  u8g2.setCursor(0, 78);
  u8g2.print("Sats: ");
  u8g2.print(gps.fixInfo.numberOfSats);
  u8g2.setCursor(70, 78);
  u8g2.print("Fix: ");
  u8g2.print(gps.fixInfo.fix);

  gpsMenuPage.drawConstellation(5, 84, 84);
}

//////////////////////////////////////////////
// Results Self-Test Page
void SelfTest_PageResults::show() { push_page(this); }

void SelfTest_PageResults::draw_extra() {
  uint8_t lineSpacing = 13;
  u8g2.setCursor(0, 30);
  u8g2.setFont(leaf_6x12);
  u8g2.print("All Tests: ");
  if (selfTest.results.allTests == SelfTest::Status::Pass) {
    u8g2.print("PASS");
  } else if (selfTest.results.allTests == SelfTest::Status::Fail) {
    u8g2.print("FAIL");
  } else {
    u8g2.print("Unknown");
  }
  u8g2.setFont(leaf_5x8);

  SelfTest::Status testResult = SelfTest::Status::Unknown;

  for (int i = 0; i < 11; i++) {
    u8g2.setCursor(0, u8g2.getCursorY() + lineSpacing);

    if (i == 0) {
      testResult = selfTest.results.sdCard;
      u8g2.print("SDCard:");
    } else if (i == 1) {
      testResult = selfTest.results.baro;
      u8g2.print("Baro:");
    } else if (i == 2) {
      testResult = selfTest.results.imu;
      u8g2.print("IMU:");
    } else if (i == 3) {
      testResult = selfTest.results.gps;
      u8g2.print("GPS:");
    } else if (i == 4) {
      testResult = selfTest.results.gpsFix;
      u8g2.print("GPS Fix:");
    } else if (i == 5) {
      testResult = selfTest.results.ambient;
      u8g2.print("Ambient:");
    } else if (i == 6) {
      testResult = selfTest.results.display;
      u8g2.print("Display:");
    } else if (i == 7) {
      testResult = selfTest.results.buttons;
      u8g2.print("Buttons:");
    } else if (i == 8) {
      testResult = selfTest.results.power;
      u8g2.print("Power:");
    } else if (i == 9) {
      testResult = selfTest.results.speaker;
      u8g2.print("Speaker:");
    } else if (i == 10) {
      testResult = selfTest.results.vario;
      u8g2.print("Vario:");
    }

    u8g2.setCursor(48, u8g2.getCursorY());
    if (testResult == SelfTest::Status::Pass) {
      u8g2.print("PASS");
    } else if (testResult == SelfTest::Status::Fail) {
      u8g2.print("FAIL");
    } else {
      u8g2.print("Unknown");
    }
  }
}

//////////////////////////////////////////////
// Commissioning Confirmation Self-Test Page
void SelfTest_PageCommissioningConfirmation::show() { push_page(this); }

void SelfTest_PageCommissioningConfirmation::draw_extra() {
  if (selfTest.commissioningCompleteConfirmed()) {
    u8g2.drawCircle(48, 94, 31);
    u8g2.drawCircle(48, 94, 30);
    u8g2.drawLine(29, 94, 42, 109);
    u8g2.drawLine(30, 94, 42, 108);
    u8g2.drawLine(42, 109, 68, 74);
    u8g2.drawLine(43, 109, 69, 74);
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(22, 148);
    u8g2.print("COMPLETE");
    return;
  }

  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(6, 32);
  u8g2.print("Awaiting");
  u8g2.setCursor(4, 43);
  u8g2.print("commissioning");
  u8g2.setCursor(8, 54);
  u8g2.print("confirmation...");
}
