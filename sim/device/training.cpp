// Training UI/lifecycle stand-in. No RF reception or delivery is simulated here.
#ifdef LEAF_TRAINING_STUDENT
#include "sim/board.h"
#include "training/radio.h"
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
  bool Radio::send(const Packet&) {
    if (!initialized_) return false;
    ++sent;
    return true;
  }
  bool Radio::receive(Packet&) { return false; }
}  // namespace training
#endif
