#ifndef PageThermalCore_h
#define PageThermalCore_h

#include <Arduino.h>

#include "ui/input/buttons.h"

void thermalCorePage_draw(void);
void thermalCorePage_button(Button button, ButtonEvent state, uint8_t count);

#endif
