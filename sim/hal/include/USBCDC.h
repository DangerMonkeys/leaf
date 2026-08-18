#pragma once
#include "HardwareSerial.h"
// With ARDUINO_USB_MODE=1 the firmware keeps using Serial; this type only has to exist.
typedef HostConsole USBCDC;
