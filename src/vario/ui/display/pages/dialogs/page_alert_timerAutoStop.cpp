#include "ui/display/pages/dialogs/page_alert_timerAutoStop.h"

#include "logging/log.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"

void PageAlertTimerAutoStop::draw_extra() {
  // Title
  display_menuTitle(String(get_title()));

  uint8_t offset = 15;
  uint8_t yPos = 30;
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(32, yPos);
  u8g2.print("Alert!");
  u8g2.setCursor(22, yPos += offset);
  u8g2.print("Timer will");
  u8g2.setCursor(10, yPos += offset);
  u8g2.print("Auto-Stop in:");
  u8g2.setCursor(20, yPos += offset);
  u8g2.print(flightTimer_getAutoStopCountRemaining());
  u8g2.print(" seconds!");
}

void PageAlertTimerAutoStop::show() { push_page(this); }

void PageAlertTimerAutoStop::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == CURSOR_BACK && state == ButtonEvent::CLICKED) {
    pop_page();
    speaker.playSound(fx::confirm);
    flightTimer_resetAutoStop();
    return;
  }
}