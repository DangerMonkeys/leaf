#ifndef Leaf_SPI_h
#define Lead_SPI_h

#include <Arduino.h>

// Note, ESP32 has four SPI busses.
// SPI0 and SPI1 -> Used for internal flash
// SPI2 -> Default bus for external peripherals (LCD, Baro, IMU).  Referred to generally as 'SPI' in
// code and variables below.
//      Note: On ESP32, SPI2 is called "FSPI" (but renamed "VSPI" by arduino references)
// SPI3 -> Used for SDcard in Quad SDIO Mode (SPI3 also called "HSPI")

// SPI and peripheral devices using the default SPI Bus (SPI2 / FSPI)
#define SPI_MOSI 11
#define SPI_CLK 12
#define SPI_MISO 13

#define SPI_SS_LCD 15

void spi_init(void);
void spi_writeGLCD(byte data);

void spi_writeIMUByte(byte address, byte data);
uint8_t spi_readIMUByte(byte address);

#endif