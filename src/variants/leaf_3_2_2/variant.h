#pragma once

// v3.2.2 uses an older display WO256X128
#define WO256X128

// For OTA updates
#define LEAF_FIRMWARE_NAME "leaf_3_2_2"

// This platform is only really used on breadboards now.
// Prevent auto-shutoff.
#define DISABLE_BATTERY_SHUTDOWN

// As the display is rotated 180 degrees, we'll just swap the
// up, down, left, right buttons
#define BUTTON_PIN_LEFT 6
#define BUTTON_PIN_RIGHT 3
#define BUTTON_PIN_UP 4
#define BUTTON_PIN_DOWN 5
