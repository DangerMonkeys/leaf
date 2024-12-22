#ifndef MENU_PAGE_H
#define MENU_PAGE_H

#include <Arduino.h>
#include "buttons.h"

class MenuPage {
  public:
    // Called whenever a button event occurs
    //   button: Button to which the event pertains
    //   state: New state of button
    //   count: (TODO: document)
    virtual bool button_event(Button button, ButtonState state, uint8_t count) = 0;

    // Called to draw the menu page.
    // Assumes(?) the screen is already clear.
    virtual void draw() = 0;

  protected:
    void cursor_prev();
    void cursor_next();

    int8_t cursor_position;   // 0 means nothing selected
    int8_t cursor_max;
};


class SettingsMenuPage : public MenuPage {
  public:
    bool button_event(Button button, ButtonState state, uint8_t count);

  protected:
    virtual void setting_change(Button dir, ButtonState state, uint8_t count) = 0;
};

#endif
