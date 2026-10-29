#pragma once
#include "battery_characterization.h"
namespace training {
  struct InstructorBattery {
    static constexpr uint32_t PERIOD_MS = 1000;
    uint32_t sampledAt = 0;
    uint16_t millivolts = 0;
    uint8_t percent = 0;
    bool valid = false, charging = false, chargingKnown = false;
    bool usb = false, usbKnown = false;
    bool due(uint32_t now) const { return !valid || uint32_t(now - sampledAt) >= PERIOD_MS; }
    void sample(uint32_t now, uint32_t adcMV, bool chargeKnown, bool charge, bool hasCurrentSense,
                uint16_t currentMA, bool inputKnown, bool input) {
      sampledAt = now;
      millivolts = battery_characterization::voltageFromAdc(adcMV);
      chargingKnown = chargeKnown;
      charging = chargeKnown && charge;
      usbKnown = inputKnown;
      usb = inputKnown && input;
      percent = battery_characterization::percentForReading(millivolts, charging, hasCurrentSense,
                                                            currentMA, percent);
      valid = true;
    }
    char icon() const { return battery_characterization::icon(percent, charging); }
    bool empty() const { return valid && millivolts <= battery_characterization::SHUTDOWN_MV; }
  };
}  // namespace training
