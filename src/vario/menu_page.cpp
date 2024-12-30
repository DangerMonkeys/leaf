#include "menu_page.h"

#include "buttons.h"
#include "display.h"
#include "fonts.h"
#include "etl/array.h"

etl::array<const char*, 0> SimpleSettingsMenuPage::emptyMenu{};

void MenuPage::cursor_prev() {
    cursor_position--;
    if (cursor_position < cursor_min) cursor_position = cursor_max;
}

void MenuPage::cursor_next() {
    cursor_position++;
    if (cursor_position > cursor_max) cursor_position = cursor_min;
}

bool SettingsMenuPage::button_event(Button button, ButtonState state, uint8_t count) {      
  switch (button) {
    case Button::UP:
      if (state == RELEASED) cursor_prev();      
      break;
    case Button::DOWN:
      if (state == RELEASED) cursor_next();      
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
  if (button != Button::NONE && state != NO_STATE) redraw = true;
  return redraw;   //update display after button push so that the UI reflects any changes immediately
}

void MenuPage::push_page(MenuPage* page) {
    get_current_page_stack().push(page);
    page->shown();
}

void MenuPage::pop_page() { get_current_page_stack().pop(); }

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
        u8g2.setFont(leaf_6x12);
        u8g2.setCursor(2, 12);
        u8g2.setDrawColor(1);
        u8g2.print(get_title());
        u8g2.drawHLine(0, 15, 95);

        // Draw the cursor selection box
        const auto BOX_X = 74 - 10;
        const auto BOX_Y = (cursor_position == CURSOR_BACK ? 190 : 45 + (cursor_position * 15)) - 14;
        u8g2.drawRBox(BOX_X, BOX_Y, 34, 16, 2);

        // Draw the back item
        u8g2.setCursor(2, 190);
        u8g2.print("Back");
        u8g2.setCursor(74, 190);
        u8g2.setDrawColor(cursor_position == CURSOR_BACK ? 0 : 1);
        u8g2.print((char)124); // Print back button
        u8g2.setDrawColor(1);

        // Draw the menu items starting from the top
        uint8_t y_pos = 45;
        for (int i = 0; i <= cursor_max; i++) {
            // Print the menu label
            u8g2.setDrawColor(1);
            u8g2.setCursor(2, y_pos);
            u8g2.print(get_labels()[i]);

            // Print the menu input
            u8g2.setCursor(74, y_pos);
            u8g2.setDrawColor(cursor_position == i ? 0 : 1);
            // Call the virtual function to draw the input
            draw_menu_input(i);
            
            y_pos += 15;
        }

        // Draw any extra elements
        draw_extra();

    } while (u8g2.nextPage());

    // Update the pages event loop
    loop();
}

// By default, print an enter character
void SimpleSettingsMenuPage::draw_menu_input(int8_t cursor_position) {
  u8g2.print((char)126);
}

// By default, a menu item will have no labels, an empty view
etl::array_view<const char*> SimpleSettingsMenuPage::get_labels() const {
  return etl::array_view<const char*>(emptyMenu);
}

// Only handle the default back button closing this dialog
void SimpleSettingsMenuPage::setting_change(Button dir, ButtonState state, uint8_t count) {
  if(cursor_position == CURSOR_BACK && state == RELEASED) {
    pop_page();
    return;
  }
}