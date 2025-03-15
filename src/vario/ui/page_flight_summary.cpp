#include "page_flight_summary.h"

#include "Arduino.h"
#include "display.h"
#include "displayFields.h"
#include "fonts.h"
#include "settings.h"
#include "string_utils.h"

void PageFlightSummary::draw_extra() {
  u8g2.setFont(leaf_6x12);

  // Flight Time
  u8g2.setCursor(0, 37);
  u8g2.print("Timer: " + formatSeconds(stats.duration, false, 0));

  // Maximuim Values
  uint8_t y = 50;
  uint8_t lineSpacing = 14;
  uint8_t indent = 6;
  u8g2.drawRFrame(2, y + 2, 92, 92, 7);
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, y, 53, 7);
  u8g2.setDrawColor(1);
  u8g2.setCursor(0, y += 5);
  u8g2.print("Maximums");

  u8g2.setCursor(indent, y += lineSpacing);
  u8g2.print("Alt:   " + formatAlt(stats.alt_max, UNITS_alt, true));
  u8g2.setCursor(indent, y += lineSpacing);
  u8g2.print("AbvTO: " + formatAlt(stats.alt_above_launch_max, UNITS_alt, true));
  u8g2.setCursor(indent, y += lineSpacing);
  u8g2.print("Climb: " + formatClimbRate(stats.climb_max, UNITS_climb, true));
  u8g2.setCursor(indent, y += lineSpacing);
  u8g2.print("Sink:  " + formatClimbRate(stats.climb_min, UNITS_climb, true));
  u8g2.setCursor(indent, y += lineSpacing);
  u8g2.print("Speed:   " + formatSpeed(stats.speed_max, UNITS_speed, true));
  u8g2.setCursor(indent, y += lineSpacing);
  u8g2.print("Accel: " + formatAccel(stats.accel_min, false) + '/' +
             formatAccel(stats.accel_max, true));
}