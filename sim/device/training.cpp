// Training UI/lifecycle stand-in. No RF reception or delivery is simulated here.
#ifdef LEAF_TRAINING_STUDENT
#include "sim/board.h"
#include "sim/clock.h"
#include "sim/training_radio.h"
#include "training/radio.h"
namespace {
  bool capture = false, pending = false;
  training::Packet injected;
  std::vector<sim::training_radio::Frame> frames;
}
namespace sim::training_radio {
  void beginCapture() { capture = true; frames.clear(); pending = false; }
  void endCapture() { capture = false; frames.clear(); pending = false; }
  const std::vector<Frame>& sent() { return frames; }
  void inject(const ::training::Packet& packet) { injected = packet; pending = true; }
}
namespace training {
  uint32_t deviceId() { return 123; }
  bool Radio::begin() {
    initialized_ = sim::board().fanetPresent;
    return initialized_;
  }
  bool Radio::end(bool) {
    initialized_ = transmitting_ = false;
    return true;
  }
  bool Radio::send(const Packet& packet) {
    if (!initialized_) return false;
    ++sent;
    if (capture && frames.size() < 256) frames.push_back({packet, sim::clock().millis()});
    return true;
  }
  bool Radio::receive(Packet& packet) {
    if (!initialized_ || !pending) return false;
    pending = false;
    packet = injected;
    ++received;
    return true;
  }
}  // namespace training
#endif
