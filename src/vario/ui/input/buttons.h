#pragma once

#include <cstdint>

// D pad button
enum class Button : uint8_t { NONE, UP, DOWN, LEFT, RIGHT, CENTER };

// D pad button state
enum ButtonState { NO_STATE, PRESSED, RELEASED, HELD, HELD_LONG };
