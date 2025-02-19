#ifndef PageThermalSimple_h
#define PageThermalSimple_h

#include <Arduino.h>

#include "buttons.h"

// draw the pixels to the display
void thermalSimplePage_draw(void);

// handle button presses relative to what's shown on the display
void thermalSimplePage_button(Button button, ButtonState state, uint8_t count);

// draw the selectable user fields
void drawUserField(uint8_t x, uint8_t y, uint8_t field, bool selected);

// user field selection options
enum class ThermSimpPageUserFields { ABOVE_LAUNCH, GLIDE, TEMP, ACCEL, DIST, AIRSPEED, NONE };

#endif