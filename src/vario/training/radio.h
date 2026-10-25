#pragma once
#if defined(LEAF_TRAINING_STUDENT) || defined(LEAF_TRAINING_INSTRUCTOR)
#include <Arduino.h>
#include <RadioLib.h>
#include "protocol.h"
namespace training {
  // Single owner, no RF work in bus handlers. Updates never wait for packet airtime.
  class Radio {
   public:
    bool begin();
    bool end(bool sleep = false);
    bool receive(Packet& packet);
    bool send(const Packet& packet);
    bool ready() const { return initialized_; }
    bool busy() const { return transmitting_; }
    uint32_t received = 0, errors = 0, sent = 0;
    int16_t lastError = 0;

   private:
    void restartRx();
    Module module_{46, 15, 0, 16};
    SX1262 chip_{&module_};
    bool initialized_ = false, transmitting_ = false, started_ = false;
    uint32_t txAt_ = 0;
    uint8_t bytes_[MAX_PACKET]{};
  };
  uint32_t deviceId();
}  // namespace training
#endif
