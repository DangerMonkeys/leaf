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
  virtual bool button_event(Button button, ButtonState state, uint8_t count) = 0;

  // Called to draw the menu page.
  // Assumes(?) the screen is already clear.
  virtual void draw() = 0;

  // Returns the current modal page on the stack.
  // If there is no modal page, returns NULL.
  MenuPage* get_modal_page();

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
  bool button_event(Button button, ButtonState state, uint8_t count);

 protected:
  virtual void setting_change(Button dir, ButtonState state, uint8_t count) = 0;
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

  // Array of labels for the menu items
  virtual etl::array_view<const char*> get_labels() const;

 protected:
  virtual void setting_change(Button dir, ButtonState state, uint8_t count) override;
  static etl::array<const char*, 0> emptyMenu;
};
