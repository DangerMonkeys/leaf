#include "protocol.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
namespace training {
  namespace {
    uint16_t crc(const uint8_t* p, size_t n) {
      uint16_t v = 0xffff;
      while (n--) {
        v ^= uint16_t(*p++) << 8;
        for (unsigned i = 0; i < 8; ++i) v = (v & 0x8000) ? (v << 1) ^ 0x1021 : v << 1;
      }
      return v;
    }
    void put(uint8_t*& p, uint32_t v, unsigned n) {
      while (n--) {
        *p++ = v & 255;
        v >>= 8;
      }
    }
    uint32_t get(const uint8_t*& p, unsigned n) {
      uint32_t v = 0;
      for (unsigned i = 0; i < n; ++i) v |= uint32_t(*p++) << (8 * i);
      return v;
    }
    bool validData(const Telemetry& d) {
      return d.slot < STUDENTS && !(d.valid & ~31) && d.toneHz <= 10000 &&
             (!(d.valid & Position) || (d.latitudeE7 >= -900000000 && d.latitudeE7 <= 900000000 &&
                                        d.longitudeE7 >= -1800000000 &&
                                        d.longitudeE7 <= 1800000000 && d.courseCdeg < 36000)) &&
             (!(d.valid & Baro) || (d.pressurePa > 0 && d.pressurePa <= 150000)) &&
             (!(d.valid & Utc) ||
              (d.date >= 20000101 && d.date <= 20991231 && d.date % 100 >= 1 &&
               d.date % 100 <= 31 && d.date / 100 % 100 >= 1 && d.date / 100 % 100 <= 12 &&
               d.time / 1000000 < 24 && d.time / 10000 % 100 < 60 && d.time / 100 % 100 < 60));
    }
    size_t finish(char* out, size_t cap, int n) {
      if (n < 0 || size_t(n) + 6 > cap) return 0;
      uint8_t sum = 0;
      for (int i = 1; i < n; ++i) sum ^= out[i];
      snprintf(out + n, cap - n, "*%02X\r\n", sum);
      return n + 5;
    }
  }  // namespace
  bool newer(uint32_t next, uint32_t previous) {
    return next != previous && uint32_t(next - previous) < 0x80000000U;
  }
  size_t encode(const Packet& f, uint8_t* out, size_t cap) {
    const bool control = f.header.type >= Type::Hello && f.header.type <= Type::Confirm;
    size_t n = f.header.type == Type::Poll        ? 24
               : f.header.type == Type::Telemetry ? 64
               : control                          ? 31
                                                  : 0;
    if (!out || !n || cap < n || !f.header.group || !f.header.sender || !f.header.session ||
        (f.header.type == Type::Telemetry && !validData(f.data)) ||
        (f.header.type == Type::Poll && (f.requestedSlot >= STUDENTS || f.focusSlot >= STUDENTS)) ||
        (control && (!f.header.sequence || f.assignedSlot >= STUDENTS ||
                     (f.header.type == Type::Hello ? (f.header.group != DISCOVERY_GROUP ||
                                                      f.target || f.targetSession || f.assignedSlot)
                                                   : (!f.target || !f.targetSession)))))
      return 0;
    uint8_t* p = out;
    put(p, 0x4c54, 2);
    put(p, 1, 1);
    put(p, uint8_t(f.header.type), 1);
    for (auto v : {f.header.group, f.header.sender, f.header.session, f.header.sequence})
      put(p, v, 4);
    if (f.header.type == Type::Poll) {
      put(p, f.requestedSlot, 1);
      put(p, f.focusSlot, 1);
    } else if (control) {
      put(p, f.target, 4);
      put(p, f.targetSession, 4);
      put(p, f.assignedSlot, 1);
    } else {
      const auto& d = f.data;
      put(p, d.slot, 1);
      put(p, d.valid, 1);
      put(p, d.gpsAge, 2);
      put(p, d.pressureAge, 2);
      put(p, d.toneHz, 2);
      put(p, d.pressurePa, 4);
      put(p, uint32_t(d.latitudeE7), 4);
      put(p, uint32_t(d.longitudeE7), 4);
      put(p, uint32_t(d.gpsAltitudeCm), 4);
      put(p, uint32_t(d.baroAltitudeCm), 4);
      put(p, uint16_t(d.climbCms), 2);
      put(p, d.speedCms, 2);
      put(p, d.courseCdeg, 2);
      put(p, d.date, 4);
      put(p, d.time, 4);
    }
    put(p, crc(out, n - 2), 2);
    return n;
  }
  bool decode(const uint8_t* in, size_t n, Packet& out) {
    if (!in || (n != 24 && n != 31 && n != 64)) return false;
    const uint8_t* p = in;
    if (get(p, 2) != 0x4c54 || get(p, 1) != 1) return false;
    Packet f;
    f.header.type = Type(get(p, 1));
    if (!((f.header.type == Type::Poll && n == 24) ||
          (f.header.type == Type::Telemetry && n == 64) ||
          (f.header.type >= Type::Hello && f.header.type <= Type::Confirm && n == 31)))
      return false;
    f.header.group = get(p, 4);
    f.header.sender = get(p, 4);
    f.header.session = get(p, 4);
    f.header.sequence = get(p, 4);
    if (f.header.type == Type::Poll) {
      f.requestedSlot = get(p, 1);
      f.focusSlot = get(p, 1);
    } else if (n == 31) {
      f.target = get(p, 4);
      f.targetSession = get(p, 4);
      f.assignedSlot = get(p, 1);
    } else {
      auto& d = f.data;
      d.slot = get(p, 1);
      d.valid = get(p, 1);
      d.gpsAge = get(p, 2);
      d.pressureAge = get(p, 2);
      d.toneHz = get(p, 2);
      d.pressurePa = get(p, 4);
      d.latitudeE7 = int32_t(get(p, 4));
      d.longitudeE7 = int32_t(get(p, 4));
      d.gpsAltitudeCm = int32_t(get(p, 4));
      d.baroAltitudeCm = int32_t(get(p, 4));
      d.climbCms = int16_t(get(p, 2));
      d.speedCms = get(p, 2);
      d.courseCdeg = get(p, 2);
      d.date = get(p, 4);
      d.time = get(p, 4);
    }
    if (get(p, 2) != crc(in, n - 2)) return false;
    uint8_t check[MAX_PACKET];
    if (encode(f, check, sizeof(check)) != n) return false;
    out = f;
    return true;
  }
  uint8_t scheduledStudent(uint8_t tick, uint8_t focus) {
    if (focus >= STUDENTS || tick >= 8 || tick == 7) return 255;
    if (!(tick & 1)) return focus;
    uint8_t other = tick / 2;
    return other >= focus ? other + 1 : other;
  }
  void Receiver::expect(uint8_t slot, uint32_t seq, uint32_t now) {
    pendingSlot_ = slot;
    pendingSequence_ = seq;
    pendingAt_ = now;
  }
  void Receiver::cancel() { pendingSlot_ = 255; }
  bool Receiver::accept(const Packet& f, uint32_t now, bool pairing) {
    if (f.header.type != Type::Telemetry || !group || f.header.group != group ||
        f.data.slot >= STUDENTS || f.data.slot != pendingSlot_ ||
        f.header.sequence != pendingSequence_ || uint32_t(now - pendingAt_) >= SLOT_MS ||
        !validData(f.data) || !f.header.sender || !f.header.session)
      return false;
    auto& s = students[f.data.slot];
    if (s.id != f.header.sender && (s.id || !pairing)) return false;
    s.id = f.header.sender;
    s.session = f.header.session;
    s.data = f.data;
    s.receivedAt = now;
    s.received = true;
    cancel();
    return true;
  }
  bool Receiver::fresh(uint32_t now) const {
    return focus < STUDENTS && students[focus].received &&
           uint32_t(now - students[focus].receivedAt) < LINK_STALE_MS;
  }
  bool Receiver::positionFresh(uint32_t now) const {
    return fresh(now) && (students[focus].data.valid & Position) &&
           uint32_t(now - students[focus].receivedAt) + students[focus].data.gpsAge < 2500;
  }
  bool Receiver::climbFresh(uint32_t now) const {
    return fresh(now) && (students[focus].data.valid & Climb) &&
           uint32_t(now - students[focus].receivedAt) + students[focus].data.pressureAge < 750;
  }
  size_t nmea(const Receiver& r, uint32_t now, unsigned sentence, char* out, size_t cap) {
    if (!out || cap < 8 || r.focus >= STUDENTS || sentence > 2) return 0;
    const auto& d = r.students[r.focus].data;
    if (sentence == 2) {
      const bool pressure = r.fresh(now) && (d.valid & Baro) &&
                            uint32_t(now - r.students[r.focus].receivedAt) + d.pressureAge < 750;
      // LK8000 Docs/LK8EX1.txt defines these unavailable-value sentinels.
      return finish(out, cap,
                    snprintf(out, cap, "$LK8EX1,%lu,%ld,%d,99,999,",
                             (unsigned long)(pressure ? d.pressurePa : 999999),
                             (long)(pressure ? d.baroAltitudeCm / 100 : 99999),
                             r.climbFresh(now) ? int(d.climbCms) : 9999));
    }
    if (!r.positionFresh(now) || !(d.valid & Utc))
      return finish(
          out, cap,
          snprintf(out, cap, sentence == 0 ? "$GPGGA,,,,,,0,00,,,,,,," : "$GPRMC,,V,,,,,,,,,"));
    // Integer rounding carries 59.99995 minutes into the next degree correctly.
    auto coordinate = [](int32_t value, bool longitude, char* result, size_t capacity) {
      const uint64_t magnitude = value < 0 ? uint64_t(-int64_t(value)) : uint64_t(value);
      const uint64_t minutes = (magnitude * 600000 + 5000000) / 10000000;
      snprintf(result, capacity, longitude ? "%03u%02u.%04u" : "%02u%02u.%04u",
               unsigned(minutes / 600000), unsigned(minutes / 10000 % 60),
               unsigned(minutes % 10000));
    };
    char latitude[16], longitude[16];
    coordinate(d.latitudeE7, false, latitude, sizeof(latitude));
    coordinate(d.longitudeE7, true, longitude, sizeof(longitude));
    char stamp[16];
    snprintf(stamp, sizeof(stamp), "%06lu.%02lu", (unsigned long)(d.time / 100),
             (unsigned long)(d.time % 100));
    char ns = d.latitudeE7 < 0 ? 'S' : 'N', ew = d.longitudeE7 < 0 ? 'W' : 'E';
    if (sentence == 0)
      return finish(out, cap,
                    snprintf(out, cap, "$GPGGA,%s,%s,%c,%s,%c,1,00,,%.2f,M,,M,,", stamp, latitude,
                             ns, longitude, ew, d.gpsAltitudeCm / 100.0));
    unsigned date = (d.date % 100) * 10000 + (d.date / 100 % 100) * 100 + d.date / 10000 % 100;
    return finish(out, cap,
                  snprintf(out, cap, "$GPRMC,%s,A,%s,%c,%s,%c,%.2f,%.2f,%06u,,,A", stamp, latitude,
                           ns, longitude, ew, d.speedCms * .019438444, d.courseCdeg / 100.0, date));
  }
}  // namespace training
