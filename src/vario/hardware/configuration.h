#pragma once

// Pins that are not connected
#define NC -1

// Variant information is defined in src/variants/{variant name}
// See documentation for src/scripts/prebuild.py for more information
#include "variant.h"

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
#define SPEAKER_PIN 14  // default for 3.2.3
#endif

#ifndef SPEAKER_VOLA
#define SPEAKER_VOLA 15      // default for 3.2.3
#define SPEAKER_VOLA_IOEX 0  // default 3.2.3 not on IO expander
#endif

#ifndef SPEAKER_VOLB
#define SPEAKER_VOLB 16      // default for 3.2.3
#define SPEAKER_VOLB_IOEX 0  // default 3.2.3 not on IO expander
#endif

// **** Power Configuration and Management ****
#ifndef POWER_GOOD
#define POWER_GOOD NC  // feature not available on default 3.2.3 config
#define POWER_GOOD_IOEX 0
#endif

#ifndef POWER_CHARGE_I1
#define POWER_CHARGE_I1 41  // default 3.2.3
#define POWER_CHARGE_I1_IOEX 0
#endif

#ifndef POWER_CHARGE_I2
#define POWER_CHARGE_I2 42  // default 3.2.3
#define POWER_CHARGE_I2_IOEX 0
#endif

#ifndef POWER_CHARGE_GOOD
#define POWER_CHARGE_GOOD 47  // default 3.2.3
#define POWER_CHARGE_GOOD_IOEX 0
#endif

// **** SD Card ****
#ifndef SD_DETECT
#define SD_DETECT 26  // input detect pin on default 3.2.3 config
#define SD_DETECT_IOEX 0
#endif

// **** GPS ****
#ifndef GPS_BACKUP_EN
#define GPS_BACKUP_EN 40  // Enable GPS backup power.
// Generally always-on, except able to be turned off for a full GPS reset if needed
#define GPS_BACKUP_EN_IOEX 0
#endif

#ifndef GPS_RESET
#define GPS_RESET 45
#define GPS_RESET_IOEX 0
#endif