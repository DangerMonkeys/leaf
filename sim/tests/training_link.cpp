#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "training/protocol.h"
using namespace training;
static Packet sample(uint8_t slot = 0) {
  Packet f;
  f.header = {Type::Telemetry, 71, 123, 45, 99};
  auto& d = f.data;
  d.slot = slot;
  d.valid = Position | Baro | Climb | Utc | Tone;
  d.gpsAge = 0;
  d.pressureAge = 0;
  d.toneHz = 1200;
  d.pressurePa = 101325;
  d.latitudeE7 = 471234567;
  d.longitudeE7 = -1221234567;
  d.gpsAltitudeCm = 12345;
  d.baroAltitudeCm = -3400;
  d.climbCms = -123;
  d.speedCms = 1000;
  d.courseCdeg = 35999;
  d.date = 20261025;
  d.time = 12345678;
  return f;
}
static void checksum(const char* s) {
  assert(s[0] == '$');
  const char* end = strchr(s, '*');
  assert(end);
  unsigned value = 0, expected = 0;
  for (const char* p = s + 1; p < end; ++p) value ^= uint8_t(*p);
  assert(sscanf(end + 1, "%2x", &expected) == 1 && value == expected);
  assert(!strcmp(end + 3, "\r\n"));
}
int main() {
  auto f = sample();
  uint8_t bytes[MAX_PACKET];
  assert(encode(f, bytes, sizeof(bytes)) == 64);
  assert(bytes[0] == 0x54 && bytes[1] == 0x4c && bytes[2] == 1 && bytes[3] == 2 && bytes[4] == 71);
  Packet decoded;
  assert(decode(bytes, 64, decoded));
  assert(decoded.data.longitudeE7 == f.data.longitudeE7 && decoded.data.climbCms == -123 &&
         decoded.data.baroAltitudeCm == -3400);
  assert(!encode(f, bytes, 63));
  for (size_t i = 0; i < 64; ++i)
    for (unsigned b = 0; b < 8; ++b) {
      bytes[i] ^= 1 << b;
      assert(!decode(bytes, 64, decoded));
      bytes[i] ^= 1 << b;
    }
  for (size_t n = 0; n < 64; ++n) assert(!decode(bytes, n, decoded));
  assert(!decode(nullptr, 64, decoded));
  auto invalid = f;
  invalid.data.slot = 4;
  assert(!encode(invalid, bytes, 64));
  invalid = f;
  invalid.data.latitudeE7 = 900000001;
  assert(!encode(invalid, bytes, 64));
  invalid = f;
  invalid.data.valid = 128;
  assert(!encode(invalid, bytes, 64));
  invalid = f;
  invalid.data.time = 24600000;
  assert(!encode(invalid, bytes, 64));
  Packet poll;
  poll.header = {Type::Poll, 71, 321, 46, 100};
  poll.requestedSlot = 3;
  poll.focusSlot = 2;
  assert(encode(poll, bytes, sizeof(bytes)) == 24 && decode(bytes, 24, decoded));
  assert(decoded.focusSlot == 2 && decoded.requestedSlot == 3);
  for (unsigned focus = 0; focus < 4; ++focus) {
    unsigned counts[5]{};
    for (unsigned tick = 0; tick < 8; ++tick) {
      auto n = scheduledStudent(tick, focus);
      ++counts[n == 255 ? 4 : n];
    }
    for (unsigned i = 0; i < 4; ++i) assert(counts[i] == (i == focus ? 4 : 1));
    assert(counts[4] == 1);
  }
  assert(scheduledStudent(0, 4) == 255 && scheduledStudent(8, 0) == 255);
  assert(newer(0, 0xffffffff) && !newer(1, 1) && !newer(0xffffffff, 0));
  Receiver receiver;
  receiver.group = 71;
  receiver.expect(0, 99, 1000);
  assert(!receiver.accept(f, 1001, false));
  assert(receiver.accept(f, 1001, true));
  assert(!receiver.accept(f, 1002, true));
  assert(receiver.fresh(1002) && receiver.climbFresh(1002) && receiver.positionFresh(1002));
  receiver.expect(0, 99, 1100);
  auto wrong = f;
  wrong.header.sender = 124;
  assert(!receiver.accept(wrong, 1101, true));
  wrong = f;
  wrong.header.group = 72;
  assert(!receiver.accept(wrong, 1101, true));
  wrong = f;
  wrong.header.sequence = 98;
  assert(!receiver.accept(wrong, 1101, true));
  wrong = f;
  wrong.data.slot = 1;
  assert(!receiver.accept(wrong, 1101, true));
  assert(!receiver.accept(f, 1225, true));  // Poll window is half-open.
  receiver.expect(0, 99, 0xfffffff0);
  assert(receiver.accept(f, 0x10, false));  // millis wrap.
  assert(receiver.fresh(0x11));
  receiver.focus = 1;
  assert(!receiver.fresh(0x11));
  receiver.expect(1, 99, 2000);
  auto second = sample(1);
  second.header.sender = 456;
  second.data.longitudeE7 = 10000000;
  assert(receiver.accept(second, 2001, true));
  assert(receiver.students[0].id == 123 && receiver.students[1].id == 456);
  receiver.focus = 0;
  receiver.expect(0, 99, 3000);
  assert(receiver.accept(f, 3001, false));
  char line[96];
  for (unsigned sentence = 0; sentence < 3; ++sentence) {
    size_t n = nmea(receiver, 3002, sentence, line, sizeof(line));
    assert(n > 0 && n <= 82 && strlen(line) == n);
    checksum(line);
    puts(line);
  }
  assert(strstr(line, "$LK8EX1,101325,-34,-123,99,999,") != nullptr);
  assert(nmea(receiver, 3002, 0, line, 8) == 0);
  assert(!receiver.fresh(5001) && !receiver.climbFresh(5001));
  assert(nmea(receiver, 5001, 2, line, sizeof(line)) > 0);
  assert(strstr(line, "$LK8EX1,999999,99999,9999,99,999,"));
  checksum(line);
  assert(nmea(receiver, 5001, 1, line, sizeof(line)) > 0 && strstr(line, "$GPRMC,,V,"));
  checksum(line);
  receiver.students[0].data.gpsAge = 2499;
  assert(!receiver.positionFresh(3002));
  receiver.students[0].data.valid = 0;
  assert(!receiver.climbFresh(3002));
  receiver.students[0].data = f.data;
  receiver.students[0].data.latitudeE7 = 899999999;
  receiver.students[0].data.longitudeE7 = -1799999999;
  assert(nmea(receiver, 3002, 0, line, sizeof(line)) > 0);
  assert(strstr(line, "9000.0000,N,18000.0000,W"));
  checksum(line);
  assert(nmea(receiver, 3002, 3, line, sizeof(line)) == 0);
  receiver.cancel();
  assert(!receiver.accept(f, 3002, true));
  puts(
      "Training codec, corruption, scheduling, pairing, source isolation, freshness, rollover and "
      "NMEA passed");
}
