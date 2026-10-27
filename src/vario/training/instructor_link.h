#pragma once
#include "protocol.h"
namespace training {
  struct InstructorRoster {
    uint32_t group = 0;
    uint32_t ids[STUDENTS]{};
    int slotFor(uint32_t id) const {
      if (!id) return -1;
      for (uint8_t i = 0; i < STUDENTS; ++i)
        if (ids[i] == id) return i;
      return -1;
    }
    int availableSlot(uint32_t id) const {
      const int existing = slotFor(id);
      if (existing >= 0) return existing;
      for (uint8_t i = 0; i < STUDENTS; ++i)
        if (!ids[i]) return i;
      return -1;
    }
    bool valid() const {
      for (uint8_t i = 0; i < STUDENTS; ++i) {
        if (ids[i] && !group) return false;
        for (uint8_t j = i + 1; j < STUDENTS; ++j)
          if (ids[i] && ids[i] == ids[j]) return false;
      }
      return true;
    }
  };
  // No heap, radio or NVS work. The adapter must save the roster before committed().
  class InstructorLink {
   public:
    enum class Phase : uint8_t { Closed, Discovering, Offering, Confirming };
    struct Candidate {
      uint32_t id = 0, session = 0, nonce = 0, seenAt = 0;
      char pilotName[PILOT_NAME_BYTES + 1]{};
      bool fresh(uint32_t now) const { return id && uint32_t(now - seenAt) < 4000; }
    };
    Candidate candidates[STUDENTS];
    uint8_t selection = 0;
    void open(uint32_t now) {
      for (auto& c : candidates) c = {};
      selection = 0;
      phase_ = Phase::Discovering;
      openedAt_ = now;
    }
    void cancel() { phase_ = Phase::Closed; }
    bool active(uint32_t now) const {
      return phase_ != Phase::Closed && uint32_t(now - openedAt_) < PAIR_WINDOW_MS;
    }
    Phase phase(uint32_t now) const { return active(now) ? phase_ : Phase::Closed; }
    uint32_t remaining(uint32_t now) const {
      return active(now) ? (PAIR_WINDOW_MS - uint32_t(now - openedAt_) + 999) / 1000 : 0;
    }
    // Non-Closed return means a window just expired, including whether confirmation was pending.
    Phase expire(uint32_t now) {
      if (phase_ == Phase::Closed || active(now)) return Phase::Closed;
      const auto old = phase_;
      cancel();
      return old;
    }
    bool hello(const Packet& p, uint32_t now) {
      if (phase(now) != Phase::Discovering || p.header.type != Type::Hello ||
          p.header.group != DISCOVERY_GROUP || !p.header.sender || !p.header.session ||
          !p.header.sequence || p.target || p.targetSession || p.assignedSlot)
        return false;
      int index = -1;
      for (uint8_t i = 0; i < STUDENTS; ++i)
        if (candidates[i].id == p.header.sender) {
          index = i;
          break;
        }
      if (index < 0)
        for (uint8_t i = 0; i < STUDENTS; ++i)
          if (!candidates[i].fresh(now)) {
            index = i;
            break;
          }
      if (index < 0) return false;
      auto& candidate = candidates[index];
      const bool same = candidate.id == p.header.sender && candidate.session == p.header.session &&
                        candidate.nonce == p.header.sequence;
      char previous[PILOT_NAME_BYTES + 1]{};
      if (same) copyPilotName(previous, candidate.pilotName);
      candidate = {p.header.sender, p.header.session, p.header.sequence, now};
      copyPilotName(candidate.pilotName, p.pilotName[0] ? p.pilotName : previous);
      return true;
    }
    void move(int direction, uint32_t now) {
      if (phase(now) != Phase::Discovering) return;
      for (uint8_t i = 0, next = selection; i < STUDENTS; ++i) {
        next = (next + STUDENTS + direction) % STUDENTS;
        if (candidates[next].fresh(now)) {
          selection = next;
          return;
        }
      }
    }
    bool choose(const InstructorRoster& roster, uint32_t now) {
      if (phase(now) != Phase::Discovering || !roster.valid() || !roster.group ||
          selection >= STUDENTS || !candidates[selection].fresh(now))
        return false;
      const int slot = roster.availableSlot(candidates[selection].id);
      if (slot < 0) return false;
      peer_ = candidates[selection];
      group_ = roster.group;
      slot_ = slot;
      phase_ = Phase::Offering;
      retryMs_ = 500;
      sentAt_ = now - retryMs_;
      return true;
    }
    uint32_t peer() const { return peer_.id; }
    const char* peerName() const { return peer_.pilotName; }
    uint8_t slot() const { return slot_; }
    bool matchesAccept(const Packet& p, uint32_t now, uint32_t id, uint32_t session) const {
      return active(now) && (phase_ == Phase::Offering || phase_ == Phase::Confirming) &&
             p.header.type == Type::Accept && p.header.group == group_ &&
             p.header.sender == peer_.id && p.header.session == peer_.session &&
             p.header.sequence == peer_.nonce && p.target == id && p.targetSession == session &&
             p.assignedSlot == slot_;
    }
    bool needsCommit(const Packet& p, uint32_t now, uint32_t id, uint32_t session) const {
      return phase(now) == Phase::Offering && matchesAccept(p, now, id, session);
    }
    void committed(uint32_t now) {
      if (phase(now) != Phase::Offering) return;
      phase_ = Phase::Confirming;
      retryMs_ = 500;
      sentAt_ = now - retryMs_;
    }
    bool control(Packet& p, uint32_t now, uint32_t id, uint32_t session) const {
      if (!active(now) || (phase_ != Phase::Offering && phase_ != Phase::Confirming) ||
          uint32_t(now - sentAt_) < retryMs_)
        return false;
      p = {};
      p.header = {phase_ == Phase::Offering ? Type::Offer : Type::Confirm, group_, id, session,
                  peer_.nonce};
      p.target = peer_.id;
      p.targetSession = peer_.session;
      p.assignedSlot = slot_;
      return true;
    }
    void sent(uint32_t now, uint32_t retryMs = 500) {
      sentAt_ = now;
      retryMs_ = retryMs;
    }
    // Call only after the receiver validates data from the paired student.
    bool linked(const Packet& p, uint32_t now) {
      if (phase(now) != Phase::Confirming || !telemetryType(p.header.type) ||
          p.header.group != group_ || p.header.sender != peer_.id ||
          p.header.session != peer_.session || p.data.slot != slot_)
        return false;
      cancel();
      return true;
    }

   private:
    Phase phase_ = Phase::Closed;
    Candidate peer_;
    uint32_t openedAt_ = 0, sentAt_ = 0, group_ = 0, retryMs_ = 500;
    uint8_t slot_ = 0;
  };
}  // namespace training
