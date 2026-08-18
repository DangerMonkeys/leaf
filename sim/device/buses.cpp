// The shared I2C and SPI buses, and the IO expander.
//
// Replaces hardware/Leaf_I2C.cpp, hardware/Leaf_SPI.cpp and hardware/io_pins.cpp.  The expander is
// not stubbed away: its pins are virtual board pins, so the speaker's volume lines, the charge
// status inputs and card-detect all behave as they do on hardware.

#include <Arduino.h>

#include "hardware/Leaf_I2C.h"
#include "hardware/Leaf_SPI.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "sim/board.h"

SemaphoreHandle_t SpiLockGuard::spiMutex = nullptr;

void wire_init() {}

void spi_init(void) {
  if (!SpiLockGuard::spiMutex) SpiLockGuard::spiMutex = xSemaphoreCreateRecursiveMutex();
}

// The LCD is driven through u8g2, whose frame buffer the emulator reads directly, so raw display
// bytes have nowhere to go.
void spi_writeGLCD(byte data) { (void)data; }
void spi_writeIMUByte(byte address, byte data) {
  (void)address;
  (void)data;
}
uint8_t spi_readIMUByte(byte address) {
  (void)address;
  return 0;
}

void ioexInit() {
#ifdef HAS_IO_EXPANDER
  // Inputs the firmware polls on the expander need a defined resting level.  Both charge-status
  // lines are active low and idle high; card detect is active low, so a card is "present" low.
  sim::Board& b = sim::board();
#ifdef POWER_CHARGE_GOOD
  b.setInputLevel(sim::IOEX_PIN_BASE + POWER_CHARGE_GOOD, true);
#endif
#ifdef POWER_GOOD
  b.setInputLevel(sim::IOEX_PIN_BASE + POWER_GOOD, false);  // USB present
#endif
#ifdef SD_DETECT
  b.setInputLevel(sim::IOEX_PIN_BASE + SD_DETECT, false);  // card inserted
#endif
#ifdef IMU_INT
  b.setInputLevel(sim::IOEX_PIN_BASE + IMU_INT, false);
#endif
#endif
}

void ioexDigitalWrite(bool onIOEX, uint8_t pin, uint8_t value) {
  const uint16_t target = onIOEX ? (uint16_t)(sim::IOEX_PIN_BASE + pin) : pin;
  sim::board().digitalWrite(target, value);

#if defined(SPEAKER_VOLA) && defined(SPEAKER_VOLB)
  // Track the speaker's two volume lines so the UI can render (and play) beeps at the volume the
  // firmware actually selected.
  const bool volAOnIoex = SPEAKER_VOLA_IOEX != 0;
  const bool volBOnIoex = SPEAKER_VOLB_IOEX != 0;
  const uint16_t volAPin =
      volAOnIoex ? (uint16_t)(sim::IOEX_PIN_BASE + SPEAKER_VOLA) : SPEAKER_VOLA;
  const uint16_t volBPin =
      volBOnIoex ? (uint16_t)(sim::IOEX_PIN_BASE + SPEAKER_VOLB) : SPEAKER_VOLB;
  if (target == volAPin || target == volBPin) {
    sim::board().setVolumePins(sim::board().digitalRead(volAPin) != 0,
                               sim::board().digitalRead(volBPin) != 0);
  }
#endif
}

uint8_t ioexDigitalRead(bool onIOEX, uint8_t pin) {
  const uint16_t target = onIOEX ? (uint16_t)(sim::IOEX_PIN_BASE + pin) : pin;
  return (uint8_t)sim::board().digitalRead(target);
}
