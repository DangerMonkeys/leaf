#ifndef Leaf_SPI_h
#define Leaf_SPI_h

#include <Arduino.h>
#include "lock_guard.h"

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

#define SPI_SS_LCD 10

void spi_init(void);
void spi_writeGLCD(byte data);

void spi_writeIMUByte(byte address, byte data);
uint8_t spi_readIMUByte(byte address);

/// @brief Class to take out a SPI Mutex Lock
class SpiLockGuard : public LockGuard {
  friend void spi_init();

 public:
  SpiLockGuard() : LockGuard(spiMutex) {}

 private:
  static SemaphoreHandle_t spiMutex;
};

#endif