#ifndef PageThermalNav_h
#define PageThermalNav_h

#include <Arduino.h>

#include "ui/input/buttons.h"

void thermalNavPage_draw(void);
void thermalNavPage_button(Button button, ButtonEvent state, uint8_t count);

#endif
