#include "selfTest_displayScreens.h"

#include "diagnostics/self_test/selfTest.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"

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

  u8g2.setCursor(36, 160);
  if (selfTest.results.buttons == SelfTest::Status::Pass) {
    u8g2.print("PASS");
  } else if (selfTest.results.buttons == SelfTest::Status::Fail) {
    u8g2.print("FAIL");
  }
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

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(36, 160);
  if (selfTest.results.vario == SelfTest::Status::Pass) {
    u8g2.print("PASS");
  } else if (selfTest.results.vario == SelfTest::Status::Fail) {
    u8g2.print("FAIL");
  }
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
  u8g2.print("YES: right / up");
  u8g2.setCursor(0, 125);
  u8g2.print("NO: left / down");
}