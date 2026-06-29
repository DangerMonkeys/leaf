#pragma once

#include <Arduino.h>
#if !defined PIO_BUILD_SYSTEM && !defined __INTELLISENSE__
// ETL Library has a bug which makes this needed when building
// on Arduino, and only Arduino IDE.
#include "Embedded_Template_Library.h"  // NOLINT
#endif
#include <etl/array.h>
#include <etl/array_view.h>
#include <etl/stack.h>

#include "ui/input/buttons.h"

#define MENU_PAGE_STACK_SIZE 10
#define CURSOR_BACK -1  // cursor position for default back button

namespace menu_ui {
  constexpr uint8_t GLYPH_SETTINGS = 151;
  constexpr uint8_t GLYPH_LOGGING = 152;
  constexpr uint8_t GLYPH_CONNECTIVITY = 153;
  constexpr uint8_t GLYPH_GPS = 154;
  constexpr uint8_t GLYPH_FLIGHT = 155;
  constexpr uint8_t GLYPH_DEVELOPER = 156;
  constexpr uint8_t GLYPH_SETUP = 156;
  constexpr uint8_t GLYPH_WEB_APP = 157;
  constexpr uint8_t GLYPH_UNITS = 158;
  constexpr uint8_t GLYPH_ALTIMETER = 159;
  constexpr uint8_t GLYPH_WIFI = 160;
  constexpr uint8_t GLYPH_BLUETOOTH = 161;
  constexpr uint8_t GLYPH_DISPLAY = 162;
  constexpr uint8_t GLYPH_FANET = 163;
  constexpr uint8_t GLYPH_VARIO = 164;
  constexpr uint8_t GLYPH_NAV_DATA = 165;
  constexpr uint8_t GLYPH_NAV_POINT_SELECT = 166;
  constexpr uint8_t GLYPH_NAV_POINT_SAVE = 167;
  constexpr uint8_t GLYPH_NAV_ROUTE_SELECT = 168;
  constexpr uint8_t GLYPH_NAV_ROUTE_BUILD = 169;
  constexpr uint8_t GLYPH_RESET = 170;
  constexpr uint8_t GLYPH_BLE = 171;
  constexpr uint8_t GLYPH_GPX = 149;
  constexpr uint8_t GLYPH_ROUTE = 150;

  constexpr uint8_t ICON_OFF = 123;
  constexpr uint8_t ICON_BACK = 124;
  constexpr uint8_t ICON_ON = 125;
  constexpr uint8_t ICON_ENTER = 62;
  constexpr uint8_t ICON_X = 87;
  constexpr uint8_t ICON_BACK_X = 80;
  constexpr uint8_t ICON_CHECKBOX_X = 83;
  constexpr uint8_t HOLD_X = 65;

  void printGlyph(uint8_t glyph);
  void printGlyphLabel(uint8_t glyph, const char* label);
  void drawTitle(const char* title, uint8_t glyph = 0);
  void beginRow(uint8_t y, bool selected, uint8_t height = 15);
  void endRow();
  void drawLabel(uint8_t x, uint8_t y, const char* label, uint8_t glyph = 0);
  void drawEnterIcon(uint8_t x, uint8_t y, bool selected);
  void drawBackIcon(uint8_t x, uint8_t y);
}  // namespace menu_ui

// Base class for all Pages to be drawn with Menu Items with support for
// modal pages.
// This class is pure virtual and must be inherited by a class that implements
// the draw() and button_event() functions.
//
// Modal Behavior:
// There's a static stack of MenuPages that is used to keep track of the current
// page.  If there's a page on the stack, this should receive button events and
// been drawn.  It is expected that the Back button typically pops from this
// stack.
class MenuPage {
 public:
  // Called whenever a button event occurs
  //   button: Button to which the event pertains
  //   state: New state of button
  //   count: (TODO: document)
  // Returns true if the page should be redrawn after the event.
  virtual bool button_event(Button button, ButtonEvent state, uint8_t count) = 0;

  // Called to draw the menu page.
  // Assumes(?) the screen is already clear.
  virtual void draw() = 0;

  // Returns the current modal page on the stack.
  // If there is no modal page, returns NULL.
  MenuPage* get_modal_page();

  // Pops all modal pages off the stack, returning to a blank slate with no
  // modal pages
  void pop_all_pages();

 protected:
  void cursor_prev();
  void cursor_next();

  // Pushes a new modal page onto the stack to receive draw events
  static void push_page(MenuPage* page);

  // Pops the current modal page off the stack
  static void pop_page();

  // Called when a modal page is shown
  virtual void shown() {};

  // Called when a modal page is closed, both when a different modal
  // dialog is shown, and when it is compleatly removed from the stack
  virtual void closed(bool removed_from_Stack) {}

  int8_t cursor_position;
  int8_t cursor_max;
  int8_t cursor_min = 0;

 private:
  // Pages
  static etl::stack<MenuPage*, MENU_PAGE_STACK_SIZE>& get_current_page_stack() {
    static etl::stack<MenuPage*, MENU_PAGE_STACK_SIZE> current_page_stack;
    return current_page_stack;
  }
};

class SettingsMenuPage : public MenuPage {
 public:
  virtual bool button_event(Button button, ButtonEvent state, uint8_t count);

 protected:
  virtual void setting_change(Button dir, ButtonEvent state, uint8_t count) = 0;
};

// A simple helper class to handle simple menu items that draw things like
// Titles and Labels.
class SimpleSettingsMenuPage : public SettingsMenuPage {
 public:
  SimpleSettingsMenuPage();

  // Method to draw the page
  void draw() override;

  // Method called to draw any extra elements to the framebuffer
  virtual void draw_extra() {};

  // Called after a page is drawn for screens to update their local states
  virtual void loop() {};

  // Called when a page is shown to the user
  void shown() override;

  // Called to draw the icon/input for a menu item (the right hand side)
  virtual void draw_menu_input(int8_t cursor_position);

  // Title of this page
  virtual const char* get_title() const = 0;
  virtual uint8_t get_title_glyph() const { return 0; }

  // Array of labels for the menu items
  virtual etl::array_view<const char*> get_labels() const;

 protected:
  virtual void setting_change(Button dir, ButtonEvent state, uint8_t count) override;
  static etl::array<const char*, 0> emptyMenu;
};
