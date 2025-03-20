#ifndef PageThermal_h
#define PageThermal_h

#include <Arduino.h>

#include "ui/input/buttons.h"

// draw the pixels to the display
void thermalPage_draw(void);

// handle button presses relative to what's shown on the display
void thermalPage_button(Button button, ButtonState state, uint8_t count);

// draw the selectable user fields
void drawUserField(uint8_t x, uint8_t y, uint8_t field, bool selected);

// user field selection options
enum class ThermalPageUserFields { ABOVE_LAUNCH, GLIDE, TEMP, ACCEL, DIST, AIRSPEED, NONE };

#endif