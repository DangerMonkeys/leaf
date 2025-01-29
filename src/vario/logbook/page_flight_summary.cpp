#include "page_flight_summary.h"

#include "Arduino.h"
#include "display.h"
#include "fonts.h"
#include "displayFields.h"

void PageFlightSummary::draw_extra() {
  u8g2.setFont(leaf_6x12);
  auto y = 40;
  const auto OFFSET = 14;  // Font is 10px high, allow for margin
  u8g2.setCursor(2, y);

  // Instruction Page
  const String lines[] = {
      (String) "Duration: " + stats.duration,
      (String) "Max Alt:" + stats.alt_max,
      (String) "AbvLaunch: " + stats.alt_above_launch_max,
      (String) "MaxClimb: " + stats.climb_max,
      (String) "MaxSink: " + stats.climb_min,
      
  };

  for (auto line : lines) {
    u8g2.setCursor(0, y);
    u8g2.print(line);
    y += OFFSET;
  }
/*
  const auto start_y = 30;

  // time flight started (or ended?)
  u8g2.setFont(leaf_6x10);
  display_clockTime(0, start_y, false);

  // Heading in top center
  uint8_t heading_x = 38;
  uint8_t heading_y = start_y;
  u8g2.setFont(leaf_7x10);
  display_heading(heading_x + 8, heading_y, true);

  // Speed in upper right corner
  u8g2.setFont(leaf_8x14);
  display_speed(70, start_y + 4);
  u8g2.setFont(leaf_5h);
  u8g2.setCursor(82, start_y + 11);
  if (UNITS_speed)
    u8g2.print("MPH");
  else
    u8g2.print("KPH");

  // Timer in lower right corner
  display_flightTimer(51, 175, 0, false);
*/

}