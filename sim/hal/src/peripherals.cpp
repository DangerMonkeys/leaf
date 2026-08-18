// Globals for the peripheral buses and radios the emulator does not model.

#include <SPI.h>
#include <TCA9555.h>
#include <USB.h>
#include <Update.h>
#include <WiFi.h>
#include <Wire.h>

#include "sim/board.h"

TwoWire Wire(0);
TwoWire Wire1(1);
SPIClass SPI(0);
WiFiClass WiFi;
UpdateClass Update;
ESPUSB USB;

// The IO expander is modelled by the virtual board rather than stubbed out, because real inputs
// hang off it: card detect, charge status, and the speaker's two volume lines.
bool TCA9555::write1(uint8_t pin, uint8_t value) {
  sim::board().digitalWrite(sim::IOEX_PIN_BASE + pin, value);
  return true;
}

uint8_t TCA9555::read1(uint8_t pin) {
  return (uint8_t)sim::board().digitalRead(sim::IOEX_PIN_BASE + pin);
}
