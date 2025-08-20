#pragma once

#include <cstdint>

// D pad button
enum class Button : uint8_t { NONE, UP, DOWN, LEFT, RIGHT, CENTER };

// D pad button event
enum class ButtonEvent : uint8_t {
  // Button was initially pressed down (triggered after debouncing)
  PRESSED,

  // Button was pressed and then released without being held
  CLICKED,

  // Button was released after having been PRESSED
  RELEASED,

  // Button was held down for a short amount of time
  HELD,

  // Button was held down for a longer amount of time (always triggers after HELD)
  HELD_LONG,

  // Button was held down long enough to increment a counter while being held (see holdCount in
  // event message)
  INCREMENTED
};
