#pragma once

// First try to load up any config from variants
#ifdef VARIANT
#include "variant.h"
#endif

// Configuration options are below.

// Use the old display as found in the 3.2.2 generation
// #define WO256X128

// The name of the model's firmware to use for auto updates
// #define LEAF_FIRMWARE_NAME "leaf-3.2.2"

// Disabled battery low voltage protection.  Useful for breadboard type
// setups.
// #define DISABLE_BATTERY_SHUTDOWN

// **** D-Pad 5-button default configuration (for 3.2.3)
#ifndef BUTTON_PIN_CENTER
#define BUTTON_PIN_CENTER 2  // INPUT
#endif

#ifndef BUTTON_PIN_LEFT
#define BUTTON_PIN_LEFT 3  // INPUT
#endif

#ifndef BUTTON_PIN_DOWN
#define BUTTON_PIN_DOWN 4  // INPUT
#endif

#ifndef BUTTON_PIN_UP
#define BUTTON_PIN_UP 5  // INPUT
#endif

#ifndef BUTTON_PIN_RIGHT
#define BUTTON_PIN_RIGHT 6  // INPUT
#endif
