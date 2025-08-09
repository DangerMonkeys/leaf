/*
 * buttons.cpp
 *
 * 5-way joystick switch (UP DOWN LEFT RIGHT CENTER)
 * Button are active-high, via resistor dividers fed from "SYSPOWER", which is typically 4.4V
 * (supply from Battery Charger under USB power) or Battery voltage (when no USB power is applied)
 * Use internal pull-down resistors on button pins
 *
 */
#include "ui/input/buttons.h"

#include <Arduino.h>

#include "instruments/baro.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/menu_page.h"
#include "ui/display/pages.h"
#include "ui/display/pages/dialogs/page_warning.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/display/pages/primary/page_thermal.h"
#include "ui/display/pages/primary/page_thermal_adv.h"
#include "ui/settings/settings.h"

// button debouncing
Button button_debounce_last = Button::NONE;
uint32_t button_debounce_time =
    5;  // time in ms for stabilized button state before returning the button press
uint32_t button_time_initial = 0;
uint32_t button_time_elapsed = 0;

// button actions
Button button_last = Button::NONE;
ButtonState button_state = NO_STATE;

// in a single button-push event, track if it was ever held long enough to reach the
// "HELD" or "HELD_LONG" states (so we know not to also take action when it is released)
bool button_everHeld = false;
uint16_t button_min_hold_time = 800;   // time in ms to count a button as "held down"
uint16_t button_max_hold_time = 3500;  // time in ms to start further actions on long-holds

uint16_t button_hold_action_time_initial = 0;
// counting time between 'action steps' while holding the button
uint16_t button_hold_action_time_elapsed = 0;
// time in ms required between "action steps" while holding the button
uint16_t button_hold_action_time_limit = 500;
uint16_t button_hold_counter = 0;

Button buttons_init(void) {
  // configure pins
  pinMode(BUTTON_PIN_UP, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_DOWN, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_LEFT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_RIGHT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_CENTER, INPUT_PULLDOWN);

  auto button = buttons_inspectPins();
  return button;
}

// when holding the center button to turn on, we need to "lock" the buttons until the user releases
// the center button. Otherwise, we'll turn on, and immediately turn back off again due to the
// persistent button press.
bool centerHoldLockButtons = true;  // default to true, for the first turn on event

// call this function after performing a center-hold button action if no additional center-hold
// actions should be taken until user lets go of the center button (example: resetting timer, then
// turning off)
void buttons_lockAfterHold() {
  centerHoldLockButtons = true;  // lock from further actions until user lets go of center button
}

// If the current page is charging, handle that, otherwise direct any input to shown modal pages
// before falling back to the current page.
Button buttons_update(void) {
  Button which_button = buttons_check();
  ButtonState button_state = buttons_get_state();

  // check if we should avoid executing further actions if buttons are 'locked' due to an already
  // executed center-hold event.  This prevents multiple sequential actions being executed if user
  // keeps holding the center button (i.e., resetting timer, then turning off)
  if (centerHoldLockButtons && which_button == Button::CENTER) {
    button_state = NO_STATE;
    return which_button;  // return early without executing further tasks
  } else {
    centerHoldLockButtons = false;  // user let go of center button, so we can reset the lock.
  }

  if (which_button == Button::NONE || button_state == NO_STATE)
    return which_button;  // don't take any action if no button
  // TODO: not jumping out of this function on NO_STATE was causing speaker issues... investigate!

  power.resetAutoOffCounter();  // pressing any button should reset the auto-off counter
                                // TODO: we should probably have a counter for Auto-Timer-Off as
                                // well, and button presses should reset that.
  uint8_t currentPage = display_getPage();  // actions depend on which page we're on

  if (currentPage == page_charging) {
    switch (which_button) {
      case Button::CENTER:
        if (button_state == HELD && button_hold_counter == 1) {
          display_clear();
          display_showOnSplash();
          display_setPage(page_thermal);  // TODO: set initial page to the user's last used page
          speaker.playSound(fx::enter);
          buttons_lockAfterHold();  // lock buttons until user lets go of power button
          power.switchToOnState();
        }
        break;
      case Button::UP:
        switch (button_state) {
          case RELEASED:
            break;
          case HELD:
            power.adjustInputCurrent(1);

            speaker.playSound(fx::enter);
            break;
        }
        break;
      case Button::DOWN:
        switch (button_state) {
          case RELEASED:
            break;
          case HELD:
            power.adjustInputCurrent(-1);
            speaker.playSound(fx::exit);
            break;
        }
        break;
    }
    return which_button;
  }
  if (displayingWarning()) {
    warningPage_button(which_button, buttons_get_state(), buttons_get_hold_count());
    display_update();
    return which_button;
  }

  // If there's a modal page currently shown, we should send the button event to that page
  auto modal_page = mainMenuPage.get_modal_page();
  if (modal_page != NULL) {
    bool draw_now =
        modal_page->button_event(which_button, buttons_get_state(), buttons_get_hold_count());
    if (draw_now) display_update();
    return which_button;
  }

  if (currentPage == page_menu) {
    bool draw_now =
        mainMenuPage.button_event(which_button, buttons_get_state(), buttons_get_hold_count());
    if (draw_now) display_update();

  } else if (currentPage == page_thermal) {
    thermalPage_button(which_button, buttons_get_state(), buttons_get_hold_count());
    display_update();

  } else if (currentPage == page_thermalAdv) {
    thermalPageAdv_button(which_button, buttons_get_state(), buttons_get_hold_count());
    display_update();

  } else if (currentPage == page_nav) {
    navigatePage_button(which_button, buttons_get_state(), buttons_get_hold_count());
    display_update();

  } else if (currentPage != page_charging) {  // NOT CHARGING PAGE (i.e., our debug test page)
    switch (which_button) {
      case Button::CENTER:
        switch (button_state) {
          case HELD:
            if (button_hold_counter == 2) {
              power.shutdown();
              while (buttons_inspectPins() == Button::CENTER) {
              }  // freeze here until user lets go of power button
              display_setPage(page_charging);
            }
            break;
          case RELEASED:
            display_turnPage(page_home);
            break;
        }
        break;
      case Button::RIGHT:
        if (button_state == RELEASED) {
          display_turnPage(page_next);
          speaker.playSound(fx::increase);
        }
        break;
      case Button::LEFT:
        /* Don't allow turning page further to the left
        if (button_state == RELEASED) {
          display_turnPage(page_prev);
          speaker_playSound(fx::decrease);
        }
        */
        break;
      case Button::UP:
        switch (button_state) {
          case RELEASED:
            baro.adjustAltSetting(1, 0);
            break;
          case HELD:
            baro.adjustAltSetting(1, 1);
            break;
          case HELD_LONG:
            baro.adjustAltSetting(1, 10);
            break;
        }
        break;
      case Button::DOWN:
        switch (button_state) {
          case RELEASED:
            baro.adjustAltSetting(-1, 0);
            break;
          case HELD:
            baro.adjustAltSetting(-1, 1);
            break;
          case HELD_LONG:
            baro.adjustAltSetting(-1, 10);
            break;
        }
        break;
    }
  }
  return which_button;
}

ButtonState buttons_get_state(void) { return button_state; }

uint16_t buttons_get_hold_count(void) { return button_hold_counter; }

// the recurring call to see if user is pressing buttons.  Handles debounce and button state changes
Button buttons_check(void) {
  button_state = NO_STATE;  // assume no_state on this pass, we'll update if necessary as we go
  auto button =
      buttons_debounce(buttons_inspectPins());  // check if we have a button press in a stable state

  // reset and exit if bouncing
  if (button == Button::BOUNCE) {
    button_hold_counter = 0;
    return Button::NONE;
  }

  // if we have a state change (low to high or high to low)
  if (button != button_last) {
    button_hold_counter =
        0;  // reset hold counter because button changed -- which means it's not being held

    if (button != Button::NONE) {  // if not-none, we have a pressed button!
      button_state = PRESSED;
    } else {                   // if it IS none, we have a just-released button
      if (!button_everHeld) {  // we only want to report a released button if it wasn't already held
                               // before.  This prevents accidental immediate 'release' button
                               // actions when you let go of a held button
        button_state = RELEASED;  // just-released
        button = button_last;     // we are presently seeing "NONE", which is the release of the
                                  // previously pressed/held button, so grab that previous button to
                                  // associate with the released state
      }
      button_everHeld = false;  // we can reset this now
    }
    // otherwise we have a non-state change (button is held)
  } else if (button != Button::NONE) {
    if (button_time_elapsed >= button_max_hold_time) {
      button_state = HELD_LONG;
    } else if (button_time_elapsed >= button_min_hold_time) {
      button_state = HELD;
      button_everHeld = true;  // track that we reached the "HELD" state, so we know not to take
                               // action also when it's released
    }
  }

  // only "act" on a held button every ~500ms (button_hold_action_time_limit).  So we'll report
  // NO_STATE in between 'actions' on a held button.
  if (button_state == HELD || button_state == HELD_LONG) {
    button_hold_action_time_elapsed = millis() - button_hold_action_time_initial;
    if (button_hold_action_time_elapsed < button_hold_action_time_limit) {
      button_state = NO_STATE;
    } else {
      button_hold_counter++;
      button_hold_action_time_elapsed = 0;
      button_hold_action_time_initial = millis();
    }
  }

  /*
  Serial.print("button: ");
  Serial.print(button);
  Serial.print(" state: ");
  Serial.print(button_state);
  Serial.print(" count: ");
  Serial.println(button_hold_counter);
  */

  if (button_state != NO_STATE) {
    switch (button) {
      case Button::CENTER:
        Serial.print("button: CENTER");
        break;
      case Button::LEFT:
        Serial.print("button: LEFT  ");
        break;
      case Button::RIGHT:
        Serial.print("button: RIGHT ");
        break;
      case Button::UP:
        Serial.print("button: UP    ");
        break;
      case Button::DOWN:
        Serial.print("button: DOWN  ");
        break;
      case Button::NONE:
        Serial.print("button: NONE  ");
        break;
    }

    switch (button_state) {
      case PRESSED:
        Serial.print(" state: PRESSED  ");
        break;
      case RELEASED:
        Serial.print(" state: RELEASED ");
        break;
      case HELD:
        Serial.print(" state: HELD     ");
        break;
      case HELD_LONG:
        Serial.print(" state: HELD_LONG");
    }

    Serial.print(" hold count: ");
    Serial.println(button_hold_counter);
  }

  // save button to track for next time.
  //  ..if state is RELEASED, then button_last should be 'NONE', since we're seeing the falling edge
  //  of the previous button press
  if (button_state == RELEASED) {
    button_last = Button::NONE;
  } else {
    button_last = button;
  }
  return button;
}

// check the state of the button hardware pins (this is pulled out as a separate function so we can
// use this for a one-time check at startup)
Button buttons_inspectPins(void) {
  Button button = Button::NONE;
  if (digitalRead(BUTTON_PIN_CENTER) == HIGH)
    button = Button::CENTER;
  else if (digitalRead(BUTTON_PIN_DOWN) == HIGH)
    button = Button::DOWN;
  else if (digitalRead(BUTTON_PIN_LEFT) == HIGH)
    button = Button::LEFT;
  else if (digitalRead(BUTTON_PIN_RIGHT) == HIGH)
    button = Button::RIGHT;
  else if (digitalRead(BUTTON_PIN_UP) == HIGH)
    button = Button::UP;
  return button;
}

// check for a minimal stable time before asserting that a button has been pressed or released
Button buttons_debounce(Button button) {
  if (button != button_debounce_last) {  // if this is a new button state
    button_time_initial = millis();      // capture the initial start time
    button_time_elapsed = 0;             // and reset the elapsed time
    button_debounce_last = button;
  } else {  // this is the same button as last time, so calculate the duration of the press
    button_time_elapsed =
        millis() - button_time_initial;  // (the roll-over modulus math works on this)
    if (button_time_elapsed >= button_debounce_time) {
      return button;
    }
  }
  return Button::BOUNCE;
}
