#include "dispatch/message_injector.h"

#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "dispatch/message_types.h"
#include "hardware/aht20.h"
#include "hardware/icm_20948.h"
#include "hardware/lc86g.h"
#include "hardware/ms5611.h"

namespace {
  bool equals_const(const char* buf, size_t len, const char* literal) {
    size_t litLen = strlen(literal);
    return (len == litLen) && (memcmp(buf, literal, litLen) == 0);
  }

  // Advance to next comma or end; returns [tok_begin, tok_end) and moves pos to character AFTER the
  // comma (or len).
  inline void next_field(const char* s, size_t len, size_t& pos, size_t& out_begin,
                         size_t& out_end) {
    out_begin = pos;
    while (pos < len && s[pos] != ',') ++pos;
    out_end = pos;
    if (pos < len && s[pos] == ',') ++pos;  // skip comma for next call
  }

  // Convert substring [b,e) to signed long using strtol. Returns true on success.
  inline bool to_long(const char* s, size_t b, size_t e, long& out) {
    char buf[32];
    size_t n = e - b;
    if (n >= sizeof(buf)) return false;
    memcpy(buf, s + b, n);
    buf[n] = '\0';
    char* endp = nullptr;
    out = std::strtol(buf, &endp, 10);
    return endp && *endp == '\0';
  }

  // Convert substring [b,e) to unsigned long using strtoul. Returns true on success.
  inline bool to_ulong(const char* s, size_t b, size_t e, unsigned long& out) {
    char buf[32];
    size_t n = e - b;
    if (n >= sizeof(buf)) return false;
    memcpy(buf, s + b, n);
    buf[n] = '\0';
    char* endp = nullptr;
    out = std::strtoul(buf, &endp, 10);
    return endp && *endp == '\0';
  }

  // Convert substring [b,e) to double using strtod. Returns true on success.
  inline bool to_double(const char* s, size_t b, size_t e, double& out) {
    char buf[48];
    size_t n = e - b;
    if (n >= sizeof(buf)) return false;
    memcpy(buf, s + b, n);
    buf[n] = '\0';
    char* endp = nullptr;
    out = std::strtod(buf, &endp);
    return endp && *endp == '\0';
  }

  // Convert substring [b,e) to float (via double for portability).
  inline bool to_float(const char* s, size_t b, size_t e, float& out) {
    double d;
    if (!to_double(s, b, e, d)) return false;
    out = static_cast<float>(d);
    return true;
  }
}  // namespace

bool MessageInjector::handleLine(const char* line, size_t len) {
  if (!line || len == 0) return false;

  switch (line[0]) {
    case '#':
      onComment(line, len);
      return true;
    case '!':
      onCommand(line, len);
      return true;
    case 'A':
      onAmbientUpdate(line, len);
      return true;
    case 'G':
      onGPSMessage(line, len);
      return true;
    case 'M':
      onMotionUpdate(line, len);
      return true;
    case 'P':
      onPressureUpdate(line, len);
      return true;
    default:
      return false;
  }
}

unsigned long MessageInjector::adjustedTime(unsigned long dt) {
  if (tStart_ == 0) {
    tStart_ = millis() - dt;
  }
  return tStart_ + dt;
}

void MessageInjector::onComment(const char* line, size_t len) {
  Serial.print("Injected comment: ");
  Serial.write(line + 1, len - 1);
  Serial.println();
  Serial.flush();
}

void MessageInjector::onCommand(const char* line, size_t len) {
  if (equals_const(line, len, "!Disconnect sensors")) {
    Serial.println("Disconnecting HW sensors");
    aht20.stopPublishing();
    ICM20948::getInstance().stopPublishing();
    lc86g.stopPublishing();
    ms5611.stopPublishing();
  } else if (equals_const(line, len, "!Reconnect sensors")) {
    Serial.println("Reconnecting HW sensors");
    aht20.publishTo(bus_);
    ICM20948::getInstance().publishTo(bus_);
    lc86g.publishTo(bus_);
    ms5611.publishTo(bus_);
  } else if (equals_const(line, len, "!Reset reference time")) {
    resetReferenceTime();
    Serial.println("Injection reference time reset");
  } else {
    Serial.print("Unrecognized injection command: ");
    Serial.write(line, len);
    Serial.println();
  }
}

void MessageInjector::onAmbientUpdate(const char* line, size_t len) {
  etl::imessage_bus* bus = bus_;
  if (!bus || !line || len < 3) return;  // minimal: 'A0,'
  size_t pos = 0;

  // Expect leading 'A'
  if (line[0] != 'A') return;
  ++pos;  // skip 'A'

  // Parse delta-t
  size_t b, e;
  next_field(line, len, pos, b, e);
  unsigned long dt = 0;
  (void)to_ulong(line, b, e, dt);
  (void)dt;  // AmbientUpdate doesn't carry time; parsed for validation only

  // Parse temperature (float)
  next_field(line, len, pos, b, e);
  float temp = 0.0f;
  (void)to_float(line, b, e, temp);

  // Parse relative humidity (float)
  next_field(line, len, pos, b, e);
  float rh = 0.0f;
  (void)to_float(line, b, e, rh);

  bus->receive(AmbientUpdate(temp, rh));
}

void MessageInjector::onGPSMessage(const char* line, size_t len) {
  etl::imessage_bus* bus = bus_;
  if (!bus || !line || len < 3) return;
  size_t pos = 0;

  // Expect leading 'G'
  if (line[0] != 'G') return;
  ++pos;  // skip 'G'

  // Parse delta-t
  size_t b, e;
  next_field(line, len, pos, b, e);
  unsigned long dt = 0;
  (void)to_ulong(line, b, e, dt);
  (void)dt;  // GpsMessage doesn't carry time; parsed for validation only

  bus->receive(GpsMessage(NMEAString(line + pos, len - pos)));
}

void MessageInjector::onMotionUpdate(const char* line, size_t len) {
  etl::imessage_bus* bus = bus_;
  if (!bus || !line || len < 3) return;  // minimal: 'M0,'
  size_t pos = 0;

  // Expect leading 'M'
  if (line[0] != 'M') return;
  ++pos;  // skip 'M'

  size_t b, e;

  // 1) delta-t
  next_field(line, len, pos, b, e);
  unsigned long dt = 0;
  (void)to_ulong(line, b, e, dt);
  unsigned long t = adjustedTime(dt);

  MotionUpdate m(t);

  // 2) accel presence flag ('A' or 'a')
  next_field(line, len, pos, b, e);
  char accelFlag = (b < e) ? line[b] : 'a';
  m.hasAcceleration = (accelFlag == 'A');

  // 3) ax, ay, az
  double d = 0.0;
  next_field(line, len, pos, b, e);
  to_double(line, b, e, d);
  m.ax = d;
  next_field(line, len, pos, b, e);
  to_double(line, b, e, d);
  m.ay = d;
  next_field(line, len, pos, b, e);
  to_double(line, b, e, d);
  m.az = d;

  // 4) orientation presence flag ('Q' or 'q')
  next_field(line, len, pos, b, e);
  char oriFlag = (b < e) ? line[b] : 'q';
  m.hasOrientation = (oriFlag == 'Q');

  // 5) qx, qy, qz
  next_field(line, len, pos, b, e);
  to_double(line, b, e, d);
  m.qx = d;
  next_field(line, len, pos, b, e);
  to_double(line, b, e, d);
  m.qy = d;
  next_field(line, len, pos, b, e);
  to_double(line, b, e, d);
  m.qz = d;

  bus->receive(m);
}

void MessageInjector::onPressureUpdate(const char* line, size_t len) {
  etl::imessage_bus* bus = bus_;
  if (!bus || !line || len < 3) return;  // minimal: 'P0,'
  size_t pos = 0;

  // Expect leading 'P'
  if (line[0] != 'P') return;
  ++pos;  // skip 'P'

  size_t b, e;

  // delta-t
  next_field(line, len, pos, b, e);
  unsigned long dt = 0;
  (void)to_ulong(line, b, e, dt);

  // pressure (int32)
  next_field(line, len, pos, b, e);
  long pLong = 0;
  (void)to_long(line, b, e, pLong);

  unsigned long t = adjustedTime(dt);
  int32_t p = static_cast<int32_t>(pLong);
  bus->receive(PressureUpdate(t, p));
}
