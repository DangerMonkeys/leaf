#pragma once

#include <TCA9555.h>

// For OTA updates
#define LEAF_FIRMWARE_NAME "leaf_3_2_6"

// This variant allows for optional use of a LoRa radio module (needs to be soft-enabled by user in
// menu confirguration)
// #define FANET
// #define LORA_SX1262

// ESP32 IO configuration changes for this variant
#define SX1262_NSS 46    // SPI Chip Select Pin
#define SX1262_DIO1 15   // DIO1 pin
#define SX1262_RESET 0   // Busy pin
#define SX1262_BUSY 16   // Busy pin
#define SX1262_RF_SW 26  // RF Switch pin
#define LED_PIN 47       // LED control is on GPIO47 on v3.2.6
#define ISET 7           // Input that correlates to actual charging current

// IO Expander configuration changes for this variant
#define HAS_IO_EXPANDER 1  // this variant has an IO expander
#define IOEX_ADDR 0x26     // I2C address of the IO Expander (was 0x20 on v3.2.5)

#define SPEAKER_VOLA TCA_P05  // Pin 5 on the IO Expander
#define SPEAKER_VOLA_IOEX 1   // this pin is on the IO Expander

#define SPEAKER_VOLB TCA_P04  // Pin 4 on the IO Expander
#define SPEAKER_VOLB_IOEX 1   // this pin is on the IO Expander

#define POWER_CHARGE_GOOD TCA_P10
#define POWER_CHARGE_GOOD_IOEX 1

#define POWER_GOOD TCA_P11
#define POWER_GOOD_IOEX 1

#define POWER_CHARGE_I1 TCA_P12
#define POWER_CHARGE_I1_IOEX 1

#define POWER_CHARGE_I2 TCA_P13
#define POWER_CHARGE_I2_IOEX 1

#define IMU_INT TCA_P14
#define IMU_INT_IOEX 1

#define SD_DETECT TCA_P15
#define SD_DETECT_IOEX 1

#define GPS_RESET TCA_P16
#define GPS_RESET_IOEX 1

#define GPS_BACKUP_EN TCA_P17
#define GPS_BACKUP_EN_IOEX 1

//                         pins 7......0
#define IOEX_REG_CONFIG_PORT0 0b00000000  // All outputs on the first port of the IOEX
#define IOEX_REG_OUTPUT_PORT0 0b11011111  // default output values for port 0
//                         pins 17....10
#define IOEX_REG_CONFIG_PORT1 0b00110011  // P10, 11, 14, 15 are inputs on the second port
#define IOEX_REG_OUTPUT_PORT1 0b11000100  // default output values for port 0

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
