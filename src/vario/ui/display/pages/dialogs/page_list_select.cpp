#include "ui/display/pages/dialogs/page_list_select.h"

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"

void PageListSelect::show(const char* title, const etl::array_view<const char*> entries,
                          const int& selected, void (*callback)(int)) {
  PageListSelect& instance = getInstance();
  instance.title = title;
  instance.entries = entries;
  instance.selected = selected;
  instance.callback = callback;
  push_page(&instance);
}

void PageListSelect::draw_menu_input(int8_t cursor_position) {
  char ret = ((char)123);  // Unselected
  if (cursor_position == selected) {
    ret = (char)125;  // Selected icon
  }
  u8g2.print(ret);
}

void PageListSelect::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (state != ButtonEvent::CLICKED) return;

  if (cursor_position == CURSOR_BACK) {
    speaker.playSound(fx::cancel);
    pop_page();
    return;
  }

  callback(cursor_position);
  speaker.playSound(fx::neutral);
  pop_page();
}

PageListSelect& PageListSelect::getInstance() {
  static PageListSelect instance;
  return instance;
}
