#pragma once

// For OTA updates
#define LEAF_FIRMWARE_NAME "leaf_3_2_5"

// This breadboard variant is intended to be used with a LoRa radio
#define FANET
#define LORA_SX1262
#define SX1262_NSS 46   // SPI Chip Select Pin
#define SX1262_DIO1 15  // DIO1 pin
#define SX1262_RESET 0  // Busy pin
#define SX1262_BUSY 16  // Busy pin

#define SPEAKER_VOLA -1  // Not Connected
#define SPEAKER_VOLB -1  // Not Connected
