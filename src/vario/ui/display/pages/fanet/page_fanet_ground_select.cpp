#include "ui/display/pages/fanet/page_fanet_ground_select.h"

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/pages/fanet/page_fanet_ground.h"

void PageFanetGroundSelect::show() { push_page(&getInstance()); }

void PageFanetGroundSelect::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (state != ButtonEvent::CLICKED) return;

  if (cursor_position == CURSOR_BACK) {
    speaker.playSound(fx::cancel);
    pop_page();
    return;
  }

  FanetGroundTrackingMode mode = (FanetGroundTrackingMode)cursor_position;
  PageFanetGround::show(mode);
}

void PageFanetGroundSelect::closed(bool removed_from_Stack) {
  if (!removed_from_Stack) {
    cursor_position = CURSOR_BACK;
  }
}

PageFanetGroundSelect& PageFanetGroundSelect::getInstance() {
  static PageFanetGroundSelect instance;
  return instance;
}
