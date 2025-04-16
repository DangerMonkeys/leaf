#include "hardware/io_pins.h"

#include <Wire.h>

// define our own pin names for IOExpander
#define TCA9555_PIN_NAMES

#define TCA_P00 0
#define TCA_P01 1
#define TCA_P02 2
#define TCA_P03 3
#define TCA_P04 4
#define TCA_P05 5
#define TCA_P06 6
#define TCA_P07 7
#define TCA_P10 10
#define TCA_P11 11
#define TCA_P12 12
#define TCA_P13 13
#define TCA_P14 14
#define TCA_P15 15
#define TCA_P16 16
#define TCA_P17 17

#include <TCA9555.h>

// First try to load up any config from variants
#include "variant.h"

// Pin configuration for the IO Expander (0=output 1=input)
#ifndef IOEX_REG_CONFIG_PORT0
#define IOEX_REG_CONFIG_PORT0 0b11111111  // default all inputs
#endif

#ifndef IOEX_REG_CONFIG_PORT1
#define IOEX_REG_CONFIG_PORT1 0b11111111  // default all inputs
#endif

// Create IO Expander
TCA9535 IOEX(IOEX_ADDR);

void ioexInit() {
  // start IO Expander
  bool result = IOEX.begin();
  Serial.print("IOEX.begin succes: ");
  Serial.println(result);

  // configure IO expander pins
  result = IOEX.pinMode8(0, IOEX_REG_CONFIG_PORT0);
  Serial.print("IOEX.pinModeP0 succes: ");
  Serial.println(result);
  IOEX.pinMode8(1, IOEX_REG_CONFIG_PORT1);
  Serial.print("IOEX.pinModeP1 succes: ");
  Serial.println(result);
}

void ioexDigitalWrite(bool onIOEX, uint8_t pin, uint8_t value) {
  // if we're writing a pin on the IO Expander
  if (onIOEX) {
    bool result = IOEX.write1(pin, value);
  } else {
    digitalWrite(pin, value);
  }
}

uint8_t ioexDigitalRead(bool onIOEX, uint8_t pin) {
  if (onIOEX) {
    return IOEX.read1(pin);
  } else {
    return digitalRead(pin);
  }
}
