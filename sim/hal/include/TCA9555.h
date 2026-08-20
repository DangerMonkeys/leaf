// IO-expander stand-in.  Expander pins are modelled by the virtual board (see sim/board.h) so the
// speaker volume lines, charge status inputs and card-detect behave like the real part.
#pragma once

#include <stdint.h>

// Pin naming used by the variant headers: TCA_Pxy is port x, bit y.
#define TCA_P00 0
#define TCA_P01 1
#define TCA_P02 2
#define TCA_P03 3
#define TCA_P04 4
#define TCA_P05 5
#define TCA_P06 6
#define TCA_P07 7
#define TCA_P10 8
#define TCA_P11 9
#define TCA_P12 10
#define TCA_P13 11
#define TCA_P14 12
#define TCA_P15 13
#define TCA_P16 14
#define TCA_P17 15

class TCA9555 {
 public:
  explicit TCA9555(uint8_t address, void* wire = nullptr) : address_(address) {}
  bool begin() { return true; }
  bool isConnected() { return true; }
  bool pinMode1(uint8_t pin, uint8_t mode) { return true; }
  bool write1(uint8_t pin, uint8_t value);
  uint8_t read1(uint8_t pin);

 private:
  uint8_t address_;
};
