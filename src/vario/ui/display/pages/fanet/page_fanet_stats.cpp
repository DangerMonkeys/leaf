#include "ui/display/pages/fanet/page_fanet_stats.h"

#include <Arduino.h>
#include "comms/fanet_radio.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"

void PageFanetStats::show() { push_page(&getInstance()); }

void PageFanetStats::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == CURSOR_BACK && state == ButtonEvent::CLICKED) {
    speaker.playSound(fx::cancel);
    pop_page();
  }
}

void PageFanetStats::draw_extra() {
  auto radioStats = fanetRadio.getStats();

  constexpr int yOffset = 35;
  etl::array<etl::pair<String, String>, 13> stats{
      etl::pair{(String) "State", fanetRadio.getState().c_str()},
      etl::pair{(String) "rx", String(radioStats.rx)},
      etl::pair{(String) "txSuccess", String(radioStats.txSuccess)},
      etl::pair{(String) "txFailed", String(radioStats.txFailed)},
      etl::pair{(String) "processed", String(radioStats.processed)},
      etl::pair{(String) "forwarded", String(radioStats.forwarded)},
      etl::pair{(String) "fwdMinRssiDrp", String(radioStats.fwdMinRssiDrp)},
      etl::pair{(String) "fwdNeighborDrp", String(radioStats.fwdNeighborDrp)},
      etl::pair{(String) "fwdDbBoostWeak", String(radioStats.fwdDbBoostWeak)},
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
