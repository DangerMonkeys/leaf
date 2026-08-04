#pragma once

#include <Arduino.h>

#include "ui/input/buttons.h"

namespace cpu_utilization {

  bool enabled();
  void recordButtonEvent(Button button, ButtonEvent event);
  uint8_t buttonPinMask(Button button);
  void recordBlock(uint8_t blockIndex, uint32_t startUs, uint32_t endUs, uint8_t buttonMask,
                   uint8_t displayContext);
  void writePendingReport();

}  // namespace cpu_utilization
