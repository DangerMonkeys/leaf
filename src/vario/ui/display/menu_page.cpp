#include "ui/display/menu_page.h"

#include "etl/array.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"

etl::array<const char*, 0> SimpleSettingsMenuPage::emptyMenu{};

namespace menu_ui {
  void printGlyph(uint8_t glyph) { u8g2.print((char)glyph); }

  void printGlyphLabel(uint8_t glyph, const char* label) {
    if (glyph != 0) {
      printGlyph(glyph);
      u8g2.print(' ');
    }
    u8g2.print(label);
  }

  void drawTitle(const char* title, uint8_t glyph) {
    if (glyph == 0) {
      display_menuTitle(String(title));
      return;
    }

    String titleText;
    titleText += (char)glyph;
    titleText += ' ';
    titleText += title;
    display_menuTitle(titleText);
  }

  void beginRow(uint8_t y, bool selected, uint8_t height) {
    if (selected) {
      u8g2.drawRBox(0, y - height + 1, 96, height + 1, 2);
      u8g2.setDrawColor(0);
    } else {
      u8g2.setDrawColor(1);
    }
  }

  void endRow() { u8g2.setDrawColor(1); }

  void drawLabel(uint8_t x, uint8_t y, const char* label, uint8_t glyph) {
    u8g2.setCursor(x, y);
    printGlyphLabel(glyph, label);
  }

  void drawEnterIcon(uint8_t x, uint8_t y, bool selected) {
    if (!selected) return;
    u8g2.setCursor(ICON_X, y);
    printGlyph(ICON_ENTER);
  }

  void drawBackIcon(uint8_t x, uint8_t y) {
    u8g2.setCursor(ICON_BACK_X, y);
    printGlyph(ICON_BACK);
  }
}  // namespace menu_ui

void MenuPage::cursor_prev() {
  cursor_position--;
  if (cursor_position < cursor_min) cursor_position = cursor_max;
}

void MenuPage::cursor_next() {
  cursor_position++;
  if (cursor_position > cursor_max) cursor_position = cursor_min;
}

bool SettingsMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  switch (button) {
    case Button::UP:
      if (state == ButtonEvent::CLICKED) cursor_prev();
      break;
    case Button::DOWN:
      if (state == ButtonEvent::CLICKED) cursor_next();
      break;
    case Button::LEFT:
      setting_change(Button::LEFT, state, count);
      break;
    case Button::RIGHT:
      setting_change(Button::RIGHT, state, count);
      break;
    case Button::CENTER:
      setting_change(Button::CENTER, state, count);
      break;
  }
  bool redraw = false;
  if (button != Button::NONE) redraw = true;
  return redraw;  // update display after button push so that the UI reflects any changes
                  // immediately
}

void MenuPage::push_page(MenuPage* page) {
  // Push a new page to the stack

  // let the current page know it has been closed for now
  if (!get_current_page_stack().empty()) {
    auto current_page = get_current_page_stack().top();
    // Let it know it's closed, but, will shown again once things on top of the stack are popped
    current_page->closed(false);
  }

  auto current_page = get_current_page_stack();
  get_current_page_stack().push(page);
  page->shown();
}

void MenuPage::pop_page() {
  if (get_current_page_stack().empty()) return;
  // Remove the current page from the stack, notifying the page it has been closed
  auto current_page = get_current_page_stack().top();
  get_current_page_stack().pop();
  current_page->closed(true);
}

void MenuPage::pop_all_pages() {
  // Pop all pages from the stack, notifying each page it has been closed
  while (!get_current_page_stack().empty()) {
    pop_page();
  }
}

MenuPage* MenuPage::get_modal_page() {
  if (get_current_page_stack().empty()) return NULL;
  return get_current_page_stack().top();
}

SimpleSettingsMenuPage::SimpleSettingsMenuPage() : SettingsMenuPage() {
  cursor_position = 0;
  cursor_max = 0;
  cursor_min = -1;  // The back button sits at -1
}

void SimpleSettingsMenuPage::shown() {
  cursor_position = CURSOR_BACK;
  auto labels = get_labels();
  cursor_max = labels.size() - 1;
}

void SimpleSettingsMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle(get_title(), get_title_glyph());

    // Draw the back item
    menu_ui::beginRow(190, cursor_position == CURSOR_BACK);
    menu_ui::drawLabel(2, 190, "Back");
    menu_ui::drawBackIcon(74, 190);
    menu_ui::endRow();

    // Draw the menu items starting from the top
    uint8_t y_pos = 45;
    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = cursor_position == i;
      menu_ui::beginRow(y_pos, selected);
      menu_ui::drawLabel(2, y_pos, get_labels()[i]);
      u8g2.setCursor(74, y_pos);
      draw_menu_input(i);
      menu_ui::endRow();

      y_pos += 15;
    }

    // Draw any extra elements
    draw_extra();

  } while (u8g2.nextPage());

  // Update the pages event loop
  loop();
}

// By default, print an enter character
void SimpleSettingsMenuPage::draw_menu_input(int8_t row_position) {
  if (cursor_position == row_position) {
    u8g2.setCursor(menu_ui::ICON_X, u8g2.getCursorY());
    menu_ui::printGlyph(menu_ui::ICON_ENTER);
  }
}

// By default, a menu item will have no labels, an empty view
etl::array_view<const char*> SimpleSettingsMenuPage::get_labels() const {
  return etl::array_view<const char*>(emptyMenu);
}

// Only handle the default back button closing this dialog
void SimpleSettingsMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == CURSOR_BACK && state == ButtonEvent::CLICKED) {
    pop_page();
    return;
  }
}
