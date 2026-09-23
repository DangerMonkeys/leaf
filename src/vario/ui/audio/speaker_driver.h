#pragma once

#include <Arduino.h>

namespace speaker_driver {
  void init();
  void playTone(uint32_t frequency);
}  // namespace speaker_driver