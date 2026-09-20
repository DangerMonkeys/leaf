#pragma once

#include <Arduino.h>

namespace speaker_driver {
  void init();
  void playTone(uint32_t frequency, bool smoothTransition = false);
}  // namespace speaker_driver