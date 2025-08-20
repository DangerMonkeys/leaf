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

  // Call to indicate that the current button has produced its only action, so therefore no more
  // action events (CLICKED, HELD, HELD_LONG, INCREMENTED) should be emitted.
  void consumeButton() { consumed_ = true; }

  // check the instantaneous state of the button hardware pins
  Button inspectPins();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

 private:
  // See buttons.md state transition diagram
  enum class State : uint8_t { Up, Debouncing, Down, Held, HeldLong };

  void startDebouncing(Button button);

  void report(Button button, ButtonEvent state);

  State state_ = State::Up;
  Button currentButton_ = Button::NONE;

  // Time when the current state began, when the next state transition may occur after a period of
  // time.
  uint32_t timeInitial_ = 0;

  // Time of the last time an INCREMENTED event was emitted.
  uint32_t timeLastIncrement_ = 0;

  // Number of INCREMENTED events already emitted for the current button.
  uint16_t holdCounter_ = 0;

  // True when input from the current button has been consumed and so the current button should not
  // emit any additional action events other than RELEASED.
  bool consumed_ = false;

  etl::imessage_bus* bus_ = nullptr;
};

extern Buttons buttons;
