// RadioLib stand-in for the LoRa/FANET module, which the emulator does not model.
#pragma once

#include <stdint.h>

#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_CHIP_NOT_FOUND -2
#define RADIOLIB_ERR_SPI_CMD_FAILED -706
#define RADIOLIB_ERR_TX_TIMEOUT -5
#define RADIOLIB_ERR_RX_TIMEOUT -6
#define RADIOLIB_ERR_CRC_MISMATCH -7

class Module {
 public:
  Module(uint32_t cs, uint32_t irq, uint32_t rst, uint32_t gpio) {}
};

class SX1262 {
 public:
  explicit SX1262(Module* module) {}
  explicit SX1262(Module module) {}
};
