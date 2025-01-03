#pragma once

#include <Arduino.h>

// Pinout for Leaf V3.2.0
#define BUTTON_PIN_CENTER 2  // INPUT
#define BUTTON_PIN_LEFT 3    // INPUT
#define BUTTON_PIN_DOWN 4    // INPUT
#define BUTTON_PIN_UP 5      // INPUT
#define BUTTON_PIN_RIGHT 6   // INPUT

/*
//Pinout for Breadboard
#define BUTTON_PIN_UP     40
#define BUTTON_PIN_DOWN   38
#define BUTTON_PIN_LEFT   39
#define BUTTON_PIN_RIGHT  42
#define BUTTON_PIN_CENTER 41
*/

// D pad button states.
// NOTE:  Left is -1 as to make casting to an int for settings easier
enum class Button { NONE, UP, DOWN, LEFT, RIGHT, CENTER, BOUNCE };
enum ButtonState { NO_STATE, PRESSED, RELEASED, HELD, HELD_LONG };

Button buttons_init(void);

Button buttons_check(void);
Button buttons_inspectPins(void);
Button buttons_debounce(Button button);
ButtonState buttons_get_state(void);
uint16_t buttons_get_hold_count(void);

Button buttons_update(void);  // the main task of checking and handling button pushes

void buttons_lockDuringPowerOn(
    void);  // lock the buttons during power-on until user next releases the center button
