#pragma once

#include <Arduino.h>
#include "hardware/configuration.h"

// D pad button states.
enum class Button { NONE, UP, DOWN, LEFT, RIGHT, CENTER, BOUNCE };
enum ButtonState { NO_STATE, PRESSED, RELEASED, HELD, HELD_LONG };

class Buttons {
 public:
  Button init();

  // the recurring call to see if user is pressing buttons.  Handles debounce and button state
  // changes
  Button check();

  ButtonState getState();

  uint16_t getHoldCount();

  Button update();  // the main task of checking and handling button pushes

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
};

extern Buttons buttons;
