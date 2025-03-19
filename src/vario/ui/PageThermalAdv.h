#ifndef PageThermalAdv_h
#define PageThermalAdv_h

#include <Arduino.h>

#include "ui/input/buttons.h"

// draw the pixels to the display
void thermalPageAdv_draw(void);

// handle button presses relative to what's shown on the display
void thermalPageAdv_button(Button button, ButtonState state, uint8_t count);

#endif