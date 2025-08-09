#pragma once

#include <Arduino.h>
#include "hardware/configuration.h"

// D pad button
enum class Button : uint8_t { NONE, UP, DOWN, LEFT, RIGHT, CENTER, BOUNCE };

// D pad button state
enum ButtonState { NO_STATE, PRESSED, RELEASED, HELD, HELD_LONG };

class Buttons {
 public:
  Button init();

  // the recurring call to see if user is pressing buttons.  Handles debounce and button state
  // changes
  Button check();

  ButtonState getState() { return buttonState_; }

  uint16_t getHoldCount() { return holdCounter_; }

  /// @brief the main task of checking and handling button pushes
  /// @details If the current page is charging, handle that, otherwise direct any input to shown
  /// modal pages before falling back to the current page.
  Button update();

  /// @brief lock the buttons after a center-hold event until user next releases the center button
  /// @details call this function after performing a center-hold button action if no additional
  /// center-hold actions should be taken until user lets go of the center button (example:
  /// resetting timer, then turning off)
  void lockAfterHold();

 private:
  // check the state of the button hardware pins (this is pulled out as a separate function so we
  // can use this for a one-time check at startup)
  Button inspectPins();

  // check for a minimal stable time before asserting that a button has been pressed or released
  Button debounce(Button button);

  // button debouncing
  Button debounceLast_ = Button::NONE;
  // time in ms for stabilized button state before returning the button press
  uint32_t debounceTime_ = 5;
  uint32_t timeInitial_ = 0;
  uint32_t timeElapsed_ = 0;

  // button actions
  Button buttonLast_ = Button::NONE;
  ButtonState buttonState_ = NO_STATE;

  // in a single button-push event, track if it was ever held long enough to reach the
  // "HELD" or "HELD_LONG" states (so we know not to also take action when it is released)
  bool everHeld_ = false;

  uint16_t holdActionTimeInitial_ = 0;
  uint16_t holdCounter_ = 0;

  // when holding the center button to turn on, we need to "lock" the buttons until the user
  // releases the center button. Otherwise, we'll turn on, and immediately turn back off again due
  // to the persistent button press.
  // default to true, for the first turn on event
  bool centerHoldLockButtons_ = true;
};

extern Buttons buttons;
