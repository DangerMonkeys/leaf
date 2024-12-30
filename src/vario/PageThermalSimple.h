#ifndef PageThermalSimple_h
#define PageThermalSimple_h

#include <Arduino.h>
#include "buttons.h"


// draw the pixels to the display
void thermalSimplePage_draw(void);

// handle button presses relative to what's shown on the display
void thermalSimplePage_button(Button button, ButtonState state, uint8_t count);


// user field selection options
enum class ThermSimpPageUserField1 {GLIDE, TEMP, ACCEL, DIST, NONE};

#endif