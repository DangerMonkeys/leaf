#pragma once
#include <vector>
#include "training/protocol.h"
namespace sim::training_radio {
  struct Frame {
    ::training::Packet packet;
    uint32_t at;
  };
  // Explicit bounded capture for adapter tests; no over-the-air RF is simulated.
  void beginCapture();
  void endCapture();
  const std::vector<Frame>& sent();
  void inject(const ::training::Packet& packet);
}
