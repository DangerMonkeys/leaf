#pragma once

// Pins that are not connected
#define NC -1

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

// ***** LoRa Configuration *****
// To flag support for FANET / LoRa.
// #define FANET

// If a SX1262 Radio is installed.
// #define LORA_SX1262

// For chip select pin
// #define SX1262_NSS

// For DIO1 pin (used to trigger interrupt on RX)
// #define SX1262_DIO1

// IO pin for resetting the module
// #define SX1262_RESET

// IO Pin if we're busy.
// #define SX1262_BUSY

// **** Speaker *****
#ifndef SPEAKER_PIN
#define SPEAKER_PIN 14  // 7
#endif

#ifndef SPEAKER_VOLA
#define SPEAKER_VOLA 15  // 8
#endif

#ifndef SPEAKER_VOLB
#define SPEAKER_VOLB 16  // 9
#endif
