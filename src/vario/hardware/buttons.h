#pragma once

#include <Arduino.h>
#include "etl/message_bus.h"

#include "dispatch/message_source.h"
#include "dispatch/message_types.h"
#include "ui/input/buttons.h"

class Buttons : IMessageSource {
 public:
  Button init();

  // the recurring call to see if user is pressing buttons.  Handles debounce and dispatches button
  // state changes
  void update();

  uint16_t getHoldCount() { return holdCounter_; }

  /// @brief lock the buttons after a center-hold event until user next releases the center button
  /// @details call this function after performing a center-hold button action if no additional
  /// center-hold actions should be taken until user lets go of the center button (example:
  /// resetting timer, then turning off)
  void lockAfterHold();

  // check the instantaneous state of the button hardware pins
  Button inspectPins();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

 private:
  void report(Button button, ButtonState state);

  // Check for a minimal stable time before asserting that a button has been pressed or released.
  // Returns true if button is debounced, or false while debouncing.
  bool debounce(Button& button);

  // button debouncing
  Button debounceLast_ = Button::NONE;
  // time in ms for stabilized button state before returning the button press
  uint32_t debounceTime_ = 5;
  uint32_t timeInitial_ = 0;
  uint32_t timeElapsed_ = 0;

  // in a single button-push event, track if it was ever held long enough to reach the
  // "HELD" or "HELD_LONG" states (so we know not to also take action when it is released)
  bool everHeld_ = false;

  uint32_t holdActionTimeInitial_ = 0;
  uint16_t holdCounter_ = 0;

  // when holding the center button to turn on, we need to "lock" the buttons until the user
  // releases the center button. Otherwise, we'll turn on, and immediately turn back off again due
  // to the persistent button press.
  // default to true, for the first turn on event
  bool centerHoldLockButtons_ = true;

  ButtonEvent lastEvent_{Button::NONE, NO_STATE, 0};

  etl::imessage_bus* bus_ = nullptr;
};

extern Buttons buttons;
