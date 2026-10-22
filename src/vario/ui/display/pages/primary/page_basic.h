#ifndef PageBasic_h
#define PageBasic_h

#include <Arduino.h>

#include "ui/input/buttons.h"

// draw the pixels to the display
void basicPage_draw(void);
void basicPage_reset();

// handle button presses relative to what's shown on the display
void basicPage_button(Button button, ButtonEvent state, uint8_t count);

#endif
