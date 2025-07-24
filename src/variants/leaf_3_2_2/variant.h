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

// This breadboard variant is intended to be used with a LoRa radio
#define FANET_CAPABLE
#define LORA_SX1262
#define SX1262_NSS 0     // SPI Chip Select Pin
#define SX1262_DIO1 46   // DIO1 pin
#define SX1262_RESET 39  // Busy pin
#define SX1262_BUSY 7    // Busy pin
#define SX1262_RF_SW 21  // RX Control