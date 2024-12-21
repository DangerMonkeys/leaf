#pragma once

#include <Arduino.h>

// Pinout for Leaf V3.2.0
#define BUTTON_PIN_CENTER 2 // INPUT
#define BUTTON_PIN_LEFT 3   // INPUT
#define BUTTON_PIN_DOWN 4   // INPUT
#define BUTTON_PIN_UP 5     // INPUT
#define BUTTON_PIN_RIGHT 6  // INPUT

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
enum buttons : int8_t
{
    NONE = 0,
    LEFT = -1,
    RIGHT = 1,
    UP = 2,
    DOWN = 3,
    CENTER = 4,
    BOUNCE = 5
};
enum button_states : uint8_t
{
    NO_STATE,
    PRESSED,
    RELEASED,
    HELD,
    HELD_LONG
};

buttons buttons_init(void);

buttons buttons_check(void);
buttons buttons_inspectPins(void);
buttons buttons_debounce(buttons button);
button_states buttons_get_state(void);
uint16_t buttons_get_hold_count(void);

buttons buttons_update(void); // the main task of checking and handling button pushes
