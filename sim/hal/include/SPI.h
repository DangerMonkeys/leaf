// SPI stand-in.  The display's pixels are read straight out of u8g2's frame buffer, so the bytes
// written here are discarded; the class exists so the vendored U8g2 Arduino wrapper compiles.
#pragma once

#include <stdint.h>

#include "Arduino.h"

#define SPI_MODE0 0x00
#define SPI_MODE1 0x01
#define SPI_MODE2 0x02
#define SPI_MODE3 0x03
#define MSBFIRST 1
#define LSBFIRST 0

#define SPI_CLOCK_DIV2 0x00101001
#define SPI_CLOCK_DIV4 0x00241001
#define SPI_CLOCK_DIV8 0x004c1001
#define SPI_CLOCK_DIV16 0x009c1001
#define SPI_CLOCK_DIV32 0x013c1001
#define SPI_CLOCK_DIV64 0x027c1001

class SPISettings {
 public:
  SPISettings() = default;
  SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode)
      : clock(clock), bitOrder(bitOrder), dataMode(dataMode) {}
  uint32_t clock = 1000000;
  uint8_t bitOrder = MSBFIRST;
  uint8_t dataMode = SPI_MODE0;
};

class SPIClass {
 public:
  explicit SPIClass(uint8_t spiBus = 0) {}
  void begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1) {}
  void end() {}
  void setFrequency(uint32_t frequency) {}
  void setClockDivider(uint32_t divider) {}
  void setDataMode(uint8_t dataMode) {}
  void setBitOrder(uint8_t bitOrder) {}
  void beginTransaction(SPISettings settings) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t data) { return 0; }
  uint16_t transfer16(uint16_t data) { return 0; }
  uint32_t transfer32(uint32_t data) { return 0; }
  void transfer(void* data, uint32_t size) {}
  void writeBytes(const uint8_t* data, uint32_t size) {}
  void write(uint8_t data) {}
  void write16(uint16_t data) {}
};

extern SPIClass SPI;
