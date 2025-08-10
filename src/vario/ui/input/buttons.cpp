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

Buttons buttons;

const uint16_t MIN_HOLD_TIME_MS = 800;   // time in ms to count a button as "held down"
const uint16_t MAX_HOLD_TIME_MS = 3500;  // time in ms to start further actions on long-holds

// time in ms required between "action steps" while holding the button
const uint16_t HOLD_ACTION_TIME_LIMIT_MS = 500;

Button Buttons::init() {
  // configure pins
  pinMode(BUTTON_PIN_UP, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_DOWN, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_LEFT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_RIGHT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_CENTER, INPUT_PULLDOWN);

  return inspectPins();
}

void Buttons::lockAfterHold() {
  centerHoldLockButtons_ = true;  // lock from further actions until user lets go of center button
}

Button Buttons::update() {
  Button which_button = check();
  ButtonState button_state = getState();

  // check if we should avoid executing further actions if buttons are 'locked' due to an already
  // executed center-hold event.  This prevents multiple sequential actions being executed if user
  // keeps holding the center button (i.e., resetting timer, then turning off)
  if (centerHoldLockButtons_ && which_button == Button::CENTER) {
    button_state = NO_STATE;
    return which_button;  // return early without executing further tasks
  } else {
    centerHoldLockButtons_ = false;  // user let go of center button, so we can reset the lock.
  }

  if (which_button == Button::NONE || button_state == NO_STATE)
    return which_button;  // don't take any action if no button
  // TODO: not jumping out of this function on NO_STATE was causing speaker issues... investigate!

  power.resetAutoOffCounter();  // pressing any button should reset the auto-off counter
                                // TODO: we should probably have a counter for Auto-Timer-Off as
                                // well, and button presses should reset that.
  MainPage currentPage = display.getPage();  // actions depend on which page we're on

  if (currentPage == MainPage::Charging) {
    switch (which_button) {
      case Button::CENTER:
        if (button_state == HELD && holdCounter_ == 1) {
          display.clear();
          display.showOnSplash();
          display.setPage(
              MainPage::Thermal);  // TODO: set initial page to the user's last used page
          speaker.playSound(fx::enter);
          lockAfterHold();  // lock buttons until user lets go of power button
          power.switchToOnState();
        }
        break;
      case Button::UP:
        switch (button_state) {
          case RELEASED:
            break;
          case HELD:
            power.increaseInputCurrent();

            speaker.playSound(fx::enter);
            break;
        }
        break;
      case Button::DOWN:
        switch (button_state) {
          case RELEASED:
            break;
          case HELD:
            power.decreaseInputCurrent();
            speaker.playSound(fx::exit);
            break;
        }
        break;
    }
    return which_button;
  }
  if (display.displayingWarning()) {
    warningPage_button(which_button, getState(), getHoldCount());
    display.update();
    return which_button;
  }

  // If there's a modal page currently shown, we should send the button event to that page
  auto modal_page = mainMenuPage.get_modal_page();
  if (modal_page != NULL) {
    bool draw_now = modal_page->button_event(which_button, getState(), getHoldCount());
    if (draw_now) display.update();
    return which_button;
  }

  if (currentPage == MainPage::Menu) {
    bool draw_now = mainMenuPage.button_event(which_button, getState(), getHoldCount());
    if (draw_now) display.update();

  } else if (currentPage == MainPage::Thermal) {
    thermalPage_button(which_button, getState(), getHoldCount());
    display.update();

  } else if (currentPage == MainPage::ThermalAdv) {
    thermalPageAdv_button(which_button, getState(), getHoldCount());
    display.update();

  } else if (currentPage == MainPage::Nav) {
    navigatePage_button(which_button, getState(), getHoldCount());
    display.update();

  } else if (currentPage != MainPage::Charging) {  // NOT CHARGING PAGE (i.e., our debug test page)
    switch (which_button) {
      case Button::CENTER:
        switch (button_state) {
          case HELD:
            if (holdCounter_ == 2) {
              power.shutdown();
              while (inspectPins() == Button::CENTER) {
              }  // freeze here until user lets go of power button
              display.setPage(MainPage::Charging);
            }
            break;
          case RELEASED:
            display.turnPage(PageAction::Home);
            break;
        }
        break;
      case Button::RIGHT:
        if (button_state == RELEASED) {
          display.turnPage(PageAction::Next);
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

Button Buttons::check() {
  buttonState_ = NO_STATE;  // assume no_state on this pass, we'll update if necessary as we go
  auto button = debounce(inspectPins());  // check if we have a button press in a stable state

  // reset and exit if bouncing
  if (button == Button::BOUNCE) {
    holdCounter_ = 0;
    return Button::NONE;
  }

  // if we have a state change (low to high or high to low)
  if (button != buttonLast_) {
    holdCounter_ =
        0;  // reset hold counter because button changed -- which means it's not being held

    if (button != Button::NONE) {  // if not-none, we have a pressed button!
      buttonState_ = PRESSED;
    } else {             // if it IS none, we have a just-released button
      if (!everHeld_) {  // we only want to report a released button if it wasn't already held
                         // before.  This prevents accidental immediate 'release' button
                         // actions when you let go of a held button
        buttonState_ = RELEASED;  // just-released
        button = buttonLast_;     // we are presently seeing "NONE", which is the release of the
                                  // previously pressed/held button, so grab that previous button to
                                  // associate with the released state
      }
      everHeld_ = false;  // we can reset this now
    }
    // otherwise we have a non-state change (button is held)
  } else if (button != Button::NONE) {
    if (timeElapsed_ >= MAX_HOLD_TIME_MS) {
      buttonState_ = HELD_LONG;
    } else if (timeElapsed_ >= MIN_HOLD_TIME_MS) {
      buttonState_ = HELD;
      everHeld_ = true;  // track that we reached the "HELD" state, so we know not to take
                         // action also when it's released
    }
  }

  // only "act" on a held button every ~500ms (HOLD_ACTION_TIME_LIMIT_MS).  So we'll report
  // NO_STATE in between 'actions' on a held button.
  if (buttonState_ == HELD || buttonState_ == HELD_LONG) {
    unsigned long button_hold_action_time_elapsed = millis() - holdActionTimeInitial_;
    if (button_hold_action_time_elapsed < HOLD_ACTION_TIME_LIMIT_MS) {
      buttonState_ = NO_STATE;
    } else {
      holdCounter_++;
      holdActionTimeInitial_ = millis();
    }
  }

  if (buttonState_ != NO_STATE) {
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

    switch (buttonState_) {
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
    Serial.println(holdCounter_);
  }

  // save button to track for next time.
  //  ..if state is RELEASED, then buttonLast_ should be 'NONE', since we're seeing the falling edge
  //  of the previous button press
  if (buttonState_ == RELEASED) {
    buttonLast_ = Button::NONE;
  } else {
    buttonLast_ = button;
  }
  return button;
}

Button Buttons::inspectPins() {
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

Button Buttons::debounce(Button button) {
  if (button != debounceLast_) {  // if this is a new button state
    timeInitial_ = millis();      // capture the initial start time
    timeElapsed_ = 0;             // and reset the elapsed time
    debounceLast_ = button;
  } else {  // this is the same button as last time, so calculate the duration of the press
    timeElapsed_ = millis() - timeInitial_;  // (the roll-over modulus math works on this)
    if (timeElapsed_ >= debounceTime_) {
      return button;
    }
  }
  return Button::BOUNCE;
}
