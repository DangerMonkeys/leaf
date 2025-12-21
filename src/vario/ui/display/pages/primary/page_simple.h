#ifndef PageSimple_h
#define PageSimple_h

#include <Arduino.h>

#include "ui/input/buttons.h"

// draw the pixels to the display
void simplePage_draw(void);

// handle button presses relative to what's shown on the display
void simplePage_button(Button button, ButtonEvent state, uint8_t count);

#endif