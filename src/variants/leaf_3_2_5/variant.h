#pragma once

// For OTA updates
#define LEAF_FIRMWARE_NAME "leaf_3_2_5"

// This variant is intended to be used with a LoRa radio
#define FANET_CAPABLE
#define LORA_SX1262
#define SX1262_NSS 46   // SPI Chip Select Pin
#define SX1262_DIO1 15  // DIO1 pin
#define SX1262_RESET 0  // Busy pin
#define SX1262_BUSY 16  // Busy pin
#define SX1262_RF_SW 7  // RF Switch pin

#define HAS_IO_EXPANDER 1  // this variant has an IO expander

#define SPEAKER_VOLA_IOEX 1  // this pin is on the IO Expander
#define SPEAKER_VOLA 2       // Pin 2 on the IO Expander

#define SPEAKER_VOLB_IOEX 1  // this pin is on the IO Expander
#define SPEAKER_VOLB 1       // Pin 1 on the IO Expander

#define IOEX_REG_CONFIG_PORT0 0b10000001  // P00 and P07 are inputs; others outputs
#define IOEX_REG_CONFIG_PORT1 0b00001001  // P10 and P13 are inputs; others outputs

/*
|IOEX Pin# |Function
|-----|--------|
| P00 | GPS_1PPS     (input)
| P01 | SPKR_VOL_B
| P02 | SPKR_VOL_A
| P03 | EYESPI_SDcard_CS
| P04 | No Connection
| P05 | EYESPI_GPIO_2
| P06 | EYESPI_GPIO_1
| P07 | EYESPI_BUSY  (input)

| P10 | EYESPI_INT   (input)
| P11 | EYESPI_MEM_CS
| P12 | EYESPI_TS_CS
| P13 | IMU_INT      (input)
| P14 | EX_14 *Expanded GPIO
| P15 | EX_15 *Expanded GPIO
| P16 | EX_16 *Expanded GPIO
| P17 | EX_17 *Expanded GPIO

*EYESPI is the multipurpose AdaFruit 18-pin connector ("Display 2")

*/
