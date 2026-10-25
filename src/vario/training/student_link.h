#pragma once
#include "protocol.h"
namespace training {
  struct Binding {
    uint32_t group = 0, instructor = 0;
    uint8_t slot = 0;
    bool valid() const { return group && instructor && slot < STUDENTS; }
  };
  // Pure enrollment state: persistent settings and all I/O remain in the student adapter.
  class StudentLink {
   public:
    Binding binding;
    bool enabled = false;
    void beginPair(uint32_t now, uint32_t nonce) {
      cancelPair();
      if (!enabled || !nonce) return;
      pairing_ = true;
      pairAt_ = now;
      nonce_ = nonce;
    }
    void cancelPair() { pairing_ = accepted_ = offered_ = false; }
    void setEnabled(bool value) {
      enabled = value;
      cancelPair();
      seen_ = false;
    }
    void unpair() {
      binding = {};
      cancelPair();
      seen_ = false;
    }
    bool pairing(uint32_t now) const {
      return enabled && pairing_ && uint32_t(now - pairAt_) < PAIR_WINDOW_MS;
    }
    bool offered(uint32_t now) const { return pairing(now) && offered_; }
    bool accepted(uint32_t now) const { return offered(now) && accepted_; }
    uint32_t candidate(uint32_t now) const { return offered(now) ? offer_.header.sender : 0; }
    uint8_t candidateSlot() const { return offer_.assignedSlot; }
    uint32_t nonce() const { return nonce_; }
    bool acceptOffer(uint32_t now) {
      if (!offered(now)) return false;
      accepted_ = true;
      return true;
    }
    Packet hello(uint32_t id, uint32_t session, const char* name = nullptr) const {
      Packet p;
      p.header = {Type::Hello, DISCOVERY_GROUP, id, session, nonce_};
      copyPilotName(p.pilotName, name, HELLO_NAME_BYTES);
      return p;
    }
    Packet acceptance(uint32_t id, uint32_t session) const {
      Packet p;
      p.header = {Type::Accept, offer_.header.group, id, session, nonce_};
      p.target = offer_.header.sender;
      p.targetSession = offer_.header.session;
      p.assignedSlot = offer_.assignedSlot;
      return p;
    }
    // Returns true only when the selected instructor confirms the user-approved offer.
    bool enroll(const Packet& p, uint32_t now, uint32_t id, uint32_t session) {
      if (!pairing(now) || p.target != id || p.targetSession != session ||
          p.header.sequence != nonce_ || !p.header.sender || !p.header.session || !p.header.group ||
          p.assignedSlot >= STUDENTS)
        return false;
      if (p.header.type == Type::Offer && !offered_) {
        offer_.header = p.header;
        offer_.assignedSlot = p.assignedSlot;
        offered_ = true;
      } else if (p.header.type == Type::Confirm && accepted(now) &&
                 p.header.sender == offer_.header.sender &&
                 p.header.session == offer_.header.session &&
                 p.header.group == offer_.header.group && p.assignedSlot == offer_.assignedSlot) {
        binding = {p.header.group, p.header.sender, p.assignedSlot};
        cancelPair();
        seen_ = false;
        return true;
      }
      return false;
    }
    bool poll(const Packet& p, uint32_t now) {
      if (!enabled || pairing(now) || !binding.valid() || p.header.type != Type::Poll ||
          p.header.group != binding.group || p.header.sender != binding.instructor ||
          !p.header.session || p.requestedSlot != binding.slot)
        return false;
      if (seen_ && p.header.session == peerSession_ && !newer(p.header.sequence, sequence_))
        return false;
      seen_ = true;
      peerSession_ = p.header.session;
      sequence_ = p.header.sequence;
      lastPoll_ = now;
      return true;
    }
    bool connected(uint32_t now) const {
      // Background slots are polled at 1 Hz; permit two missed polls on the student UI.
      return enabled && !pairing(now) && seen_ && uint32_t(now - lastPoll_) < 3000;
    }

   private:
    // Only retain routing fields. Never keep packet/name/telemetry buffers on the student.
    struct Offer {
      Header header;
      uint8_t assignedSlot = 0;
    } offer_;
    uint32_t pairAt_ = 0, nonce_ = 0, peerSession_ = 0, sequence_ = 0, lastPoll_ = 0;
    bool pairing_ = false, offered_ = false, accepted_ = false, seen_ = false;
  };
}  // namespace training
