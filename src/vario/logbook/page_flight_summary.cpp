#include "page_flight_summary.h"

#include "Arduino.h"
#include "display.h"
#include "fonts.h"

void PageFlightSummary::draw_extra() {
  u8g2.setFont(leaf_6x12);
  auto y = 40;
  const auto OFFSET = 14;  // Font is 10px high, allow for margin
  u8g2.setCursor(2, y);

  // Instruction Page
  const String lines[] = {
      (String) "Duration: " + stats.duration,
      (String) "Above Launch: " + stats.alt_above_launch_max,
  };

  for (auto line : lines) {
    u8g2.setCursor(0, y);
    u8g2.print(line);
    y += OFFSET;
  }
}