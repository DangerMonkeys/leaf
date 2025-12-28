#include "selfTest_displayScreens.h"

#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"

//////////////////////////////////////////////
// Button Self-Test Page
void SelfTest_PageButtons::show() { push_page(this); }

void SelfTest_PageButtons::draw_extra() {
  uint8_t buttonCluster_x = 48;
  uint8_t buttonCluster_y = 70;
  u8g2.setCursor(0, 40);
  u8g2.setFont(leaf_6x12);
  u8g2.print("Press all buttons:");
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
