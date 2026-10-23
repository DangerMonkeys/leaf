#include "system/usb_serial.h"

#if !ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#include <tusb.h>

LeafUSBCDC USBSerial(0);

size_t LeafUSBCDC::write(uint8_t byte) { return write(&byte, 1); }

size_t LeafUSBCDC::write(const uint8_t* buffer, size_t size) {
  // Do not wait for either another writer or a USB host. In particular, the loop task
  // must return to instrument processing even if a connected monitor stops reading.
  if (!buffer || !size || !transmitEnabled_.load() || !tx_lock || xPortInIsrContext()) return 0;
  if (xSemaphoreTake(tx_lock, 0) != pdPASS) return 0;
  size_t written = 0;
  if (transmitEnabled_.load() && tud_cdc_n_connected(itf)) {
    const size_t available = tud_cdc_n_write_available(itf);
    if (size > available) size = available;
    if (size) {
      written = tud_cdc_n_write(itf, buffer, size);
      if (written) tud_cdc_n_write_flush(itf);
    }
  }
  xSemaphoreGive(tx_lock);
  return written;
}

void LeafUSBCDC::flush() {
  if (!transmitEnabled_.load() || !tx_lock || xPortInIsrContext()) return;
  if (xSemaphoreTake(tx_lock, 0) != pdPASS) return;
  if (transmitEnabled_.load() && tud_cdc_n_connected(itf)) tud_cdc_n_write_flush(itf);
  xSemaphoreGive(tx_lock);
}
#endif
