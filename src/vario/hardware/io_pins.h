#pragma once
#include <Arduino.h>

/* IO Expander
[datasheet](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2304251416_XINLUDA-XL9535QF24_C5444301.pdf)

Chip is configured with I2C address: 0x20
*/

#include "variant.h"  // first try to load ioex addr from variant.h
#ifndef IOEX_ADDR
#define IOEX_ADDR 0x20
#endif

void ioexInit();

void ioexDigitalWrite(bool onIOEX, uint8_t pin, uint8_t value);
uint8_t ioexDigitalRead(bool onIOEX, uint8_t pin);
