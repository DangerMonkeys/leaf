#pragma once

#include <USBCDC.h>

#if !ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
extern USBCDC USBSerial;

#undef Serial
#define Serial USBSerial
#endif
