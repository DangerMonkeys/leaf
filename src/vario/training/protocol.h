#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pilot_name.h"
namespace training {
  constexpr uint8_t STUDENTS = 4;
  constexpr size_t MAX_PACKET = 64;
  // SLOT_MS applies only to the retained legacy request/reply decoder.
  constexpr uint32_t SLOT_MS = 125, LINK_STALE_MS = 2000;
  constexpr uint32_t FOCUS_PERIOD_MS = 1000, FOCUS_LEASE_MS = 3000;
  // V1 poll/telemetry remain byte-compatible; enrollment adds bounded control frames.
  enum class Type : uint8_t {
    Poll = 1,
    Telemetry = 2,
    Hello = 3,
    Offer = 4,
    Accept = 5,
    Confirm = 6,
    Broadcast = 9,
    Focus = 10
  };
  constexpr bool telemetryType(Type type) {
    return type == Type::Telemetry || type == Type::Broadcast;
  }
  // Jitter breaks persistent collisions between independently clocked students.
  constexpr uint32_t broadcastInterval(bool fast, uint32_t entropy) {
    return fast ? 225 + entropy % 51 : 900 + entropy % 201;
  }
  constexpr uint32_t DISCOVERY_GROUP = 0x54524e47;
  constexpr uint32_t PAIR_WINDOW_MS = 60000;
  // Tone/toneHz are legacy V1 fields, accepted but ignored by instructor audio.
  enum Valid : uint8_t { Position = 1, Baro = 2, Climb = 4, Utc = 8, Tone = 16 };
  struct Header {
    Type type = Type::Poll;
    uint32_t group = 0, sender = 0, session = 0, sequence = 0;
  };
  struct Telemetry {
    uint8_t slot = 0, valid = 0;
    uint16_t gpsAge = 65535, pressureAge = 65535, toneHz = 0;
    uint32_t pressurePa = 0;
    int32_t latitudeE7 = 0, longitudeE7 = 0, gpsAltitudeCm = 0, baroAltitudeCm = 0;
    int16_t climbCms = 0;
    uint16_t speedCms = 0, courseCdeg = 0;
    uint32_t date = 0, time = 0;  // YYYYMMDD, HHMMSScc UTC.
  };
  struct Packet {
    Header header;
    Telemetry data;
    uint8_t requestedSlot = 0, focusSlot = 0;
    // Enrollment: target identity, target boot session, assigned student slot.
    // header.sequence echoes the student's fresh pairing-window nonce.
    uint32_t target = 0, targetSession = 0;
    uint8_t assignedSlot = 0;
    char pilotName[PILOT_NAME_BYTES + 1]{};
  };
  size_t encode(const Packet&, uint8_t*, size_t);
  bool decode(const uint8_t*, size_t, Packet&);
  bool newer(uint32_t next, uint32_t previous);
  uint8_t scheduledStudent(uint8_t tick, uint8_t focus);
  struct StudentState {
    Telemetry data;
    uint32_t id = 0, session = 0, receivedAt = 0;
    uint32_t sequence = 0, previousSession = 0;
    bool received = false;
    char pilotName[PILOT_NAME_BYTES + 1]{};
  };
  class Receiver {
   public:
    StudentState students[STUDENTS];
    uint8_t focus = 0;
    uint32_t group = 0;
    void expect(uint8_t slot, uint32_t sequence, uint32_t now);
    bool accept(const Packet&, uint32_t now, bool pairing);
    bool fresh(uint32_t now) const;
    bool positionFresh(uint32_t now) const;
    bool climbFresh(uint32_t now) const;
    void cancel();

   private:
    uint8_t pendingSlot_ = 255;
    uint32_t pendingSequence_ = 0, pendingAt_ = 0;
  };
  size_t nmea(const Receiver&, uint32_t now, unsigned sentence, char* out, size_t capacity);
}  // namespace training
