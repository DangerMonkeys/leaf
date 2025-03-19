#pragma once

#include "ui/input/buttons.h"

// draw the pixels to the display
void warningPage_draw(void);

// handle button presses relative to what's shown on the display
void warningPage_button(Button button, ButtonState state, uint8_t count);