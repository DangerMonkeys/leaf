#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

#include "system/usb_serial.h"
#include "system/usb_state.h"

namespace {
  bool connected = true;
  bool inIsr = false;
  bool disconnectSucceeds = true, connectSucceeds = true;
  size_t capacity = 0;
  size_t acceptedLimit = 100;
  unsigned writes = 0, flushes = 0, lockAttempts = 0;
  std::string received;
}  // namespace

int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks) {
  assert(ticks == 0);  // A stalled host or competing writer must never hold up firmware.
  ++lockAttempts;
  if (lock->held) return 0;
  lock->held = true;
  return pdPASS;
}
void xSemaphoreGive(SemaphoreHandle_t lock) {
  assert(lock->held);
  lock->held = false;
}
bool xPortInIsrContext() { return inIsr; }
bool tud_cdc_n_connected(uint8_t) { return connected; }
uint32_t tud_cdc_n_write_available(uint8_t) { return capacity; }
uint32_t tud_cdc_n_write(uint8_t, const void* bytes, uint32_t count) {
  ++writes;
  assert(count <= capacity);
  const size_t accepted = std::min(size_t(count), acceptedLimit);
  received.append(static_cast<const char*>(bytes), accepted);
  capacity -= accepted;
  return accepted;
}
uint32_t tud_cdc_n_write_flush(uint8_t) {
  ++flushes;
  return 0;  // Deliberately no host progress, even while connected/DTR remains true.
}
uint32_t millis() { return 123; }
bool tud_disconnect() {
  // Output must already be disabled while detach itself is in progress.
  assert(USBSerial.write("during detach") == 0);
  return disconnectSucceeds;
}
bool tud_connect() { return connectSucceeds; }

int main() {
  LeafUSBCDC serial;
  assert(serial.write("before begin") == 0);
  serial.begin(115200);

  // A full FIFO with stale connected state reproduces the vendor writer's spin condition.
  for (unsigned i = 0; i < 10000; ++i) assert(serial.write("host not reading") == 0);
  assert(writes == 0 && flushes == 0 && !serial.testLock.held);

  capacity = 3;
  assert(serial.write("abcdef") == 3);
  assert(received == "abc" && writes == 1 && flushes == 1);
  assert(serial.write(uint8_t('x')) == 0);

  capacity = 10;
  serial.setTransmitEnabled(false);
  assert(serial.write("detached") == 0);
  serial.flush();
  assert(writes == 1 && flushes == 1);
  serial.setTransmitEnabled(true);
  connected = false;
  assert(serial.write("no host") == 0);
  serial.flush();
  assert(writes == 1 && flushes == 1);

  connected = true;
  serial.testLock.held = true;
  assert(serial.write("contended") == 0);
  serial.flush();
  assert(serial.testLock.held && writes == 1);
  serial.testLock.held = false;

  inIsr = true;
  const unsigned beforeIsr = lockAttempts;
  assert(serial.write("interrupt") == 0);
  serial.flush();
  assert(lockAttempts == beforeIsr);
  inIsr = false;
  assert(serial.write(nullptr, 10) == 0);
  assert(serial.write(reinterpret_cast<const uint8_t*>("x"), 0) == 0);

  acceptedLimit = 1;  // The endpoint may accept less than its advertised space.
  assert(serial.write("reconnected") == 1);
  assert(writes == 2 && received == "abcr");
  assert(serial.write(uint8_t('!')) == 1);
  serial.flush();
  assert(!serial.testLock.held && received == "abcr!");

  // Exercise the real USB lifecycle, including stale endpoint state and failed transitions.
  capacity = 100;
  acceptedLimit = 100;
  assert(leaf_usb::begin());
  assert(USBSerial.write("boot") == 4);
  assert(leaf_usb::disconnect());
  assert(USBSerial.write("detached") == 0);
  assert(leaf_usb::disconnect());
  connectSucceeds = false;
  assert(!leaf_usb::connect());
  assert(USBSerial.write("failed reconnect") == 0);
  connectSucceeds = true;
  assert(leaf_usb::connect());
  assert(USBSerial.write("reattached") == 10);
  disconnectSucceeds = false;
  assert(!leaf_usb::disconnect());
  assert(USBSerial.write("still attached") == 14);
  disconnectSucceeds = true;
  assert(leaf_usb::disconnect());
  assert(USBSerial.write("detached again") == 0);
  puts("USB serial stalled-host, disconnect, contention, and reconnect checks passed");
}
