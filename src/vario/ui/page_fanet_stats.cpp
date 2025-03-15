#include "page_fanet_stats.h"
#include <Arduino.h>
#include "display.h"
#include "fanet_radio.h"
#include "fonts.h"

void PageFanetStats::show() { push_page(&getInstance()); }

void PageFanetStats::draw_extra() {
  auto radioStats = FanetRadio::getInstance().getStats();

  constexpr int yOffset = 35;
  etl::array<etl::pair<String, String>, 13> stats{
      etl::pair{(String) "State", FanetRadio::getInstance().getState().c_str()},
      etl::pair{(String) "rx", String(radioStats.rx)},
      etl::pair{(String) "txSuccess", String(radioStats.txSuccess)},
      etl::pair{(String) "txFailed", String(radioStats.txFailed)},
      etl::pair{(String) "processed", String(radioStats.processed)},
      etl::pair{(String) "forwarded", String(radioStats.forwarded)},
      etl::pair{(String) "fwdMinRssiDrp", String(radioStats.fwdMinRssiDrp)},
      etl::pair{(String) "fwdNeighborDrp", String(radioStats.fwdNeighborDrp)},
      etl::pair{(String) "fwdEnqueuedDrp", String(radioStats.fwdEnqueuedDrop)},
      etl::pair{(String) "fwdDbBoostDrop", String(radioStats.fwdDbBoostDrop)},
      etl::pair{(String) "rxFromUsDrp", String(radioStats.rxFromUsDrp)},
      etl::pair{(String) "txAck", String(radioStats.txAck)},
      etl::pair{(String) "neighbors", String(radioStats.neighborTableSize)}};

  u8g2.setFont(leaf_5x8);
  // Show the State first
  u8g2.setCursor(15, yOffset - 8);
  u8g2.print(stats[0].second);

  for (int i = 1; i < stats.size(); i++) {
    auto y = yOffset + i * 10;
    u8g2.setCursor(0, y);
    u8g2.print(stats[i].first);
    u8g2.setCursor(80, y);
    u8g2.print(stats[i].second);
  }
}

PageFanetStats& PageFanetStats::getInstance() {
  static PageFanetStats page;
  return page;
}