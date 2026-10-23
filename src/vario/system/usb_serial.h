#pragma once

#include <USBCDC.h>

#if !ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#include <atomic>

// USB diagnostics are best effort. The bundled USBCDC writer can spin forever on a full
// FIFO after a software disconnect leaves the host's line state temporarily asserted.
class LeafUSBCDC : public USBCDC {
 public:
  using USBCDC::USBCDC;
  using USBCDC::write;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  void flush() override;
  void setTransmitEnabled(bool enabled) { transmitEnabled_.store(enabled); }

 private:
  std::atomic<bool> transmitEnabled_{true};
};

extern LeafUSBCDC USBSerial;

#undef Serial
#define Serial USBSerial
#endif
