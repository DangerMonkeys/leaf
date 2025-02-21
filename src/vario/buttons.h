#pragma once

#include <Arduino.h>
#include "configuration.h"

// D pad button states.
// NOTE:  Left is -1 as to make casting to an int for settings easier
enum class Button { NONE, UP, DOWN, LEFT, RIGHT, CENTER, BOUNCE };
enum ButtonState { NO_STATE, PRESSED, RELEASED, HELD, HELD_LONG };

Button buttons_init(void);

Button buttons_check(void);
Button buttons_inspectPins(void);
Button buttons_debounce(Button button);
ButtonState buttons_get_state(void);
uint16_t buttons_get_hold_count(void);

Button buttons_update(void);  // the main task of checking and handling button pushes

// lock the buttons after a center-hold event until user next releases the center button
void buttons_lockAfterHold(void);
