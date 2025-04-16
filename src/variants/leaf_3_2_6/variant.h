#pragma once

// For OTA updates
#define LEAF_FIRMWARE_NAME "leaf_3_2_6"

// This variant allows for optional use of a LoRa radio module (needs to be soft-enabled by user in
// menu confirguration)
#define FANET
#define LORA_SX1262

// ESP32 IO configuration changes for this variant
#define SX1262_NSS 46    // SPI Chip Select Pin
#define SX1262_DIO1 15   // DIO1 pin
#define SX1262_RESET 0   // Busy pin
#define SX1262_BUSY 16   // Busy pin
#define SX1262_RF_SW 26  // RF Switch pin
#define LED 47           // LED control is on GPIO47

// IO Expander configuration changes for this variant
#define HAS_IO_EXPANDER 1  // this variant has an IO expander

#define SPEAKER_VOLA_IOEX 1  // this pin is on the IO Expander
#define SPEAKER_VOLA 5       // Pin 5 on the IO Expander

#define SPEAKER_VOLB_IOEX 1  // this pin is on the IO Expander
#define SPEAKER_VOLB 4       // Pin 4 on the IO Expander

#define CHG_GOOD 10
#define CHG_GOOD_IOEX 1
#define PWR_GOOD 11
#define PWR_GOOD_IOEX 1
#define PWR_CHG_i1 12
#define PWR_CHG_i1_IOEX 1
#define PWR_CHG_i2 13
#define PWR_CHG_i2_IOEX 1
#define IMU_INT 14
#define IMU_INT_IOEX 1
#define SD_DETECT 15
#define SD_DETECT_IOEX 1
#define GPS_RESET 16
#define GPS_RESET_IOEX 1
#define GPS_BAK_EN 17
#define GPS_BAK_EN_IOEX 1

#define IOEX_REG_CONFIG_PORT0 0b00000000  // All outputs on the first port of the IOEX
#define IOEX_REG_CONFIG_PORT1 0b00110011  // P10, 11, 14, 15 are inputs on the second port

/*
|  IOEX  |    3.2.6    | INPUT |
|--------|-------------|-------|
| A0 P00 |EYESPI_MEM_CS|       |
| A1 P01 |EYESPI_TOUCH_CS|     |
| A2 P02 |EYESPI_GP1   |       |
| A3 P03 |EYESPI_GP2   |       |
| A4 P04 |SPKR_VOLB    |       |
| A5 P05 |SPKR_VOLA    |       |
| A6 P06 |IOEX_1       |       |
| A7 P07 |IOEX_2       |       |

| B0 P10 |CHG_GOOD     |   *   |
| B1 P11 |PWR_GOOD     |   *   |
| B2 P12 |PWR_CHG_i1   |       |
| B3 P13 |PWR_CHG_i2   |       |
| B4 P14 |IMU_INT      |   *   |
| B5 P15 |SD_DETECT    |   *   |
| B6 P16 |GPS_RESET    |       |
| B7 P17 |GPS_BAK_EN   |       |

*EYESPI is the multipurpose AdaFruit 18-pin connector ("Display 2")

*/
