#include "hardware/io_pins.h"

#include <Wire.h>

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
#ifndef LEAF_RAW_RECORDER
  Serial.print("IOEX.begin success: ");
  Serial.println(result);
#endif

// first set default output pin states (before enabling outputs)
#ifdef IOEX_REG_OUTPUT_PORT0
  result = IOEX.write8(0, IOEX_REG_OUTPUT_PORT0);
#ifndef LEAF_RAW_RECORDER
  Serial.print("IOEX.writeP0 default output success: ");
  Serial.println(result);
#endif
#endif
#ifdef IOEX_REG_OUTPUT_PORT1
  result = IOEX.write8(1, IOEX_REG_OUTPUT_PORT1);
#ifndef LEAF_RAW_RECORDER
  Serial.print("IOEX.writeP1 default output success: ");
  Serial.println(result);
#endif
#endif

  // configure IO expander pins
  result = IOEX.pinMode8(0, IOEX_REG_CONFIG_PORT0);
#ifndef LEAF_RAW_RECORDER
  Serial.print("IOEX.pinModeP0 success: ");
  Serial.println(result);
#endif
  result = IOEX.pinMode8(1, IOEX_REG_CONFIG_PORT1);
#ifndef LEAF_RAW_RECORDER
  Serial.print("IOEX.pinModeP1 success: ");
  Serial.println(result);
#endif
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
