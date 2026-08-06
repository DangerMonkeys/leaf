#ifndef PageThermalTrack_h
#define PageThermalTrack_h

#include <Arduino.h>

#include "ui/input/buttons.h"

void thermalTrackPage_draw(void);
void thermalTrackPage_button(Button button, ButtonEvent state, uint8_t count);

#endif
