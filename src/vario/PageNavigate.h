#ifndef PageNavigate_h
#define PageNavigate_h

#include <Arduino.h>



// draw the pixels to the display
void navigatePage_draw(void);

// handle button presses relative to what's shown on the display
void navigatePage_button(Button button, ButtonState state, uint8_t count);


#endif