#include "ui/display/pages/fanet/page_fanet_neighbors.h"

#include "comms/fanet_radio.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"

void PageFanetNeighbors::show() {
  static PageFanetNeighbors instance;
  push_page(&instance);
}

void PageFanetNeighbors::draw_extra() {
  const auto neighbors = FanetRadio::getInstance().getNeighborTable();

  constexpr auto x = 3;
  constexpr auto yOffset = 10;
  auto y = 25;

  u8g2.setCursor(x, y);
  u8g2.setFont(leaf_5x8);
  int i = 0;
  // For each neighbor (up to 2 max).  Render some basic stats
  for (auto it = neighbors.begin(); it != neighbors.end() && i++ < 2; it++) {
    u8g2.print((String) "Address: " + FanetAddressToString(it->second.address));
    u8g2.setCursor(x, y += yOffset);
    u8g2.print((String) "rssi: " + it->second.rssi);
    u8g2.setCursor(x, y += yOffset);
    u8g2.print((String) "snr: " + it->second.snr);
    u8g2.setCursor(x, y += yOffset);

    if (it->second.distanceKm.has_value()) {
      u8g2.print((String) "Km : " + String(it->second.distanceKm.value(), 2));
      u8g2.setCursor(x, y += yOffset);
    }

    if (it->second.groundTrackingMode.has_value()) {
      u8g2.print((String) "Gnd State: " + it->second.groundTrackingMode.value().c_str());
      u8g2.setCursor(x, y += yOffset);
    }

    u8g2.print((String) "Last Seen: " + (millis() - it->second.lastSeen) / 1000 + " seconds ago");
    u8g2.setCursor(x, y += yOffset);
    u8g2.print("---");
    u8g2.setCursor(x, y += yOffset);
  }
}
