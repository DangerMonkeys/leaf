#include "ui/display/pages/dialogs/page_list_select.h"

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

void PageListSelect::setting_change(Button dir, ButtonState state, uint8_t count) {
  // Call the parent class to handle the back button
  SimpleSettingsMenuPage::setting_change(dir, state, count);

  if (state != RELEASED) return;

  // If 255, it's the back button
  if (cursor_position != -1) {
    // Handle the selected item
    // Perform the callback
    callback(cursor_position);
  }

  // Close the selection page
  pop_page();
}

PageListSelect& PageListSelect::getInstance() {
  static PageListSelect instance;
  return instance;
}
