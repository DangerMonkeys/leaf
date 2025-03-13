#include "io_pins.h"
#include <TCA9555.h>
#include <Wire.h>

// First try to load up any config from variants
#include "variant.h"

// Pin configuration for the IO Expander (0=output 1=input)
#ifndef IOEX_REG_CONFIG_PORT0
#define IOEX_REG_CONFIG_PORT0 B11111111  // default all inputs
#endif

#ifndef IOEX_REG_CONFIG_PORT1
#define IOEX_REG_CONFIG_PORT1 B11111111  // default all inputs
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
