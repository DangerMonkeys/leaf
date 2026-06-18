#include "system/usb_serial.h"

#if !ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
USBCDC USBSerial(0);
#endif
