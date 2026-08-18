// USB state, replacing system/usb_state.cpp and system/usb_serial.cpp.
//
// The emulator's console is stdout, always available, and there is no host to mount the card, so
// the device never needs to stay awake for one.

#include <Arduino.h>

#include "system/usb_state.h"

namespace leaf_usb {

  void init() {}
  bool begin() { return true; }
  bool started() { return true; }
  bool hostMounted() { return false; }
  bool hostSuspended() { return false; }
  bool shouldStayAwakeForHost() { return false; }

}  // namespace leaf_usb
