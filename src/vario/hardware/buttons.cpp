/*
 * buttons.cpp
 *
 * 5-way joystick switch (UP DOWN LEFT RIGHT CENTER)
 * Button are active-high, via resistor dividers fed from "SYSPOWER", which is typically 4.4V
 * (supply from Battery Charger under USB power) or Battery voltage (when no USB power is applied)
 * Use internal pull-down resistors on button pins
 *
 */
#include "hardware/buttons.h"

#include <Arduino.h>

#include "hardware/configuration.h"
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
#include "ui/input/buttons.h"
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

void Buttons::report(Button button, ButtonState state) {
  lastEvent_ = ButtonEvent(button, state, holdCounter_);
  etl::imessage_bus* bus = bus_;
  if (bus) {
    bus->receive(lastEvent_);
  }
}

void Buttons::update() {
  Button button = inspectPins();

  // reset and exit if bouncing
  if (!debounce(button)) {
    holdCounter_ = 0;
    return;
  }

  // check if we should avoid executing further actions if buttons are 'locked' due to an already
  // executed center-hold event.  This prevents multiple sequential actions being executed if user
  // keeps holding the center button (i.e., resetting timer, then turning off)
  if (centerHoldLockButtons_ && button == Button::CENTER) {
    return;  // return early without executing further tasks
  } else {
    centerHoldLockButtons_ = false;  // user let go of center button, so we can reset the lock.
  }

  // if we have a state change (low to high or high to low)
  bool newButtonPressed = lastEvent_.state == RELEASED && button != Button::NONE;
  bool buttonChanged = lastEvent_.state != RELEASED && button != lastEvent_.button;
  if (newButtonPressed || buttonChanged) {
    // reset hold counter because button changed -- which means it's not being held
    holdCounter_ = 0;

    if (button != Button::NONE) {  // if not-none, we have a pressed button!
      report(button, PRESSED);
    } else {             // if it IS none, we have a just-released button
      if (!everHeld_) {  // we only want to report a released button if it wasn't already held
                         // before.  This prevents accidental immediate 'release' button
                         // actions when you let go of a held button
        // we are presently seeing "NONE", which is the release of the previously pressed/held
        // button, so grab that previous button to associate with the released state
        report(lastEvent_.button, RELEASED);
      }
      everHeld_ = false;  // we can reset this now
    }
    // otherwise we have a non-state change (button is held)
  } else if (button != Button::NONE) {
    if (timeElapsed_ >= MAX_HOLD_TIME_MS) {
      if (lastEvent_.state != HELD_LONG) {
        report(button, HELD_LONG);
      }
    } else if (timeElapsed_ >= MIN_HOLD_TIME_MS) {
      if (lastEvent_.state != HELD) {
        report(button, HELD);
      }
      everHeld_ = true;  // track that we reached the "HELD" state, so we know not to take
                         // action also when it's released
    }

    if (lastEvent_.state == HELD || lastEvent_.state == HELD_LONG) {
      // only increment the hold counter on a held button every ~500ms (HOLD_ACTION_TIME_LIMIT_MS).
      if (millis() - holdActionTimeInitial_ >= HOLD_ACTION_TIME_LIMIT_MS) {
        holdCounter_++;
        holdActionTimeInitial_ = millis();
        report(button, lastEvent_.state);
      }
    }
  }
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

bool Buttons::debounce(Button& button) {
  if (button != debounceLast_) {  // if this is a new button state
    timeInitial_ = millis();      // capture the initial start time
    timeElapsed_ = 0;             // and reset the elapsed time
    debounceLast_ = button;
  } else {  // this is the same button as last time, so calculate the duration of the press
    timeElapsed_ = millis() - timeInitial_;  // (the roll-over modulus math works on this)
    if (timeElapsed_ >= debounceTime_) {
      return true;
    }
  }
  return false;
}
