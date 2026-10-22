// Deterministic USB ownership outcomes for simulator tests. Defaults keep the
// ordinary emulator's USB hardware unavailable; no real host is ever connected.
#pragma once
namespace sim {
  void setUsbMassStorageAvailable(bool available);
  void setUsbHostMounted(bool mounted);
  void setUsbAcquireBlocked(bool blocked);
  bool usbMassStorageAvailable();
  bool usbHostMounted();
  bool usbAcquireBlocked();
}  // namespace sim
