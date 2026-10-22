// USB state, replacing system/usb_state.cpp and system/usb_serial.cpp.
//
// The emulator's console is stdout, always available, and there is no host to mount the card, so
// the device never needs to stay awake for one.

#include <Arduino.h>

#include "sim/usb.h"
#include "system/usb_state.h"

namespace {
  bool massStorageAvailable = false;
  bool hostMounted = false;
  bool acquireBlocked = false;
}  // namespace
namespace sim {
  void setUsbMassStorageAvailable(bool available) { massStorageAvailable = available; }
  void setUsbHostMounted(bool mounted) { hostMounted = mounted; }
  void setUsbAcquireBlocked(bool blocked) { acquireBlocked = blocked; }
  bool usbMassStorageAvailable() { return massStorageAvailable; }
  bool usbHostMounted() { return hostMounted; }
  bool usbAcquireBlocked() { return acquireBlocked; }
}  // namespace sim

namespace leaf_usb {

  void init() {}
  bool begin() { return true; }
  bool connect() { return true; }
  bool disconnect() { return true; }
  bool started() { return true; }
  bool hostMounted() { return sim::usbHostMounted(); }
  bool hostSuspended() { return false; }
  bool shouldStayAwakeForHost() { return false; }

}  // namespace leaf_usb
