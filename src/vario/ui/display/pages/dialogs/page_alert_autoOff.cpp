#include "ui/display/pages/dialogs/page_alert_autoOff.h"

#include "power.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"

void PageAlertAutoOff::draw_extra() {
  // Title
  display_menuTitle(String(get_title()));

  uint8_t offset = 15;
  uint8_t yPos = 40;
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(32, yPos);
  u8g2.print("Alert!");
  u8g2.setCursor(22, yPos += 2 * offset);
  u8g2.print("Leaf will");
  u8g2.setCursor(10, yPos += offset);
  u8g2.print("shutdown in:");
  u8g2.setCursor(20, yPos += offset);
  u8g2.print(power.getAutoOffSecondsRemaining());
  u8g2.print(" seconds!");
  u8g2.setCursor(32, yPos += 2 * offset);
  u8g2.print("Press");
  u8g2.setCursor(17, yPos += offset);
  u8g2.print("any button");
  u8g2.setCursor(20, yPos += offset);
  u8g2.print("to cancel");
}

void PageAlertAutoOff::show() { push_page(this); }

void PageAlertAutoOff::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == CURSOR_BACK && state == ButtonEvent::CLICKED) {
    pop_page();
    speaker.playSound(fx::confirm);
    power.resetAutoOffCounter();  // reset the auto off counter if user acknowledges the alert. This
                                  // happens on every button push, so we shouldn't strictly need the
                                  // call here too, but adding for clarity and redundancy
    return;
  }
}