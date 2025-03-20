#include "ui/display/pages/fanet/page_fanet_ground_select.h"

#include "ui/display/pages/fanet/page_fanet_ground.h"

void PageFanetGroundSelect::show() { push_page(&getInstance()); }

void PageFanetGroundSelect::setting_change(Button dir, ButtonState state, uint8_t count) {
  // Call the parent method
  SimpleSettingsMenuPage::setting_change(dir, state, count);
  if (cursor_position == CURSOR_BACK) return;

  if (state != ButtonState::RELEASED) {
    return;
  }

  // Show the tracking page
  FanetGroundTrackingMode mode = (FanetGroundTrackingMode)cursor_position;
  PageFanetGround::show(mode);
}

PageFanetGroundSelect& PageFanetGroundSelect::getInstance() {
  static PageFanetGroundSelect instance;
  return instance;
}
