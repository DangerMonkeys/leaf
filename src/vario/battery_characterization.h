#pragma once

#include <stdint.h>

namespace battery_characterization {

  // First-pass effective resistance of the cell, contacts, and charging path. The voltage measured
  // at the battery terminals rises by approximately I * R while charging; keep the raw reading for
  // diagnostics and use this compensated voltage only for the state-of-charge display.
  constexpr uint16_t CHARGE_PATH_MILLIOHMS = 260;
  constexpr uint16_t LEGACY_CHARGE_CURRENT_MA = 410;
  constexpr uint16_t MAX_CHARGE_CURRENT_MA = 810;  // ISET fast-charge limit

  constexpr uint16_t estimatedChargeCurrentMA(uint16_t batteryMV, bool hasCurrentSense,
                                              uint16_t measuredCurrentMA) {
    if (hasCurrentSense) {
      return measuredCurrentMA < MAX_CHARGE_CURRENT_MA ? measuredCurrentMA : MAX_CHARGE_CURRENT_MA;
    }

    // Older boards cannot measure charge current. Approximate the constant-current phase as 410mA,
    // allowing for about 90mA of system load at the charger's 500mA input setting,
    // then taper linearly over the 4.0-4.2V constant-voltage phase.
    if (batteryMV <= 4000) return LEGACY_CHARGE_CURRENT_MA;
    if (batteryMV >= 4200) return 0;
    return static_cast<uint16_t>((4200 - batteryMV) * LEGACY_CHARGE_CURRENT_MA / 200);
  }

  constexpr uint16_t voltageForPercent(uint16_t batteryMV, bool charging, bool hasCurrentSense,
                                       uint16_t measuredCurrentMA) {
    if (!charging) return batteryMV;
    const uint32_t riseMV = static_cast<uint32_t>(estimatedChargeCurrentMA(
                                batteryMV, hasCurrentSense, measuredCurrentMA)) *
                            CHARGE_PATH_MILLIOHMS / 1000;
    return batteryMV > riseMV ? static_cast<uint16_t>(batteryMV - riseMV) : 0;
  }

  constexpr uint16_t FULL_MV = 4080, EMPTY_MV = 3250, SHUTDOWN_MV = 3200;
  constexpr uint16_t voltageFromAdc(uint32_t adcMV) { return adcMV * 69 / 41; }
  constexpr uint8_t percentForReading(uint16_t batteryMV, bool charging, bool hasCurrentSense,
                                      uint16_t measuredCurrentMA, uint8_t previous) {
    const uint16_t mv = voltageForPercent(batteryMV, charging, hasCurrentSense, measuredCurrentMA);
    const uint8_t percent = mv < EMPTY_MV  ? 0
                            : mv > FULL_MV ? 100
                                           : 100 * (mv - EMPTY_MV) / (FULL_MV - EMPTY_MV);
    // Match Leaf's directional 2% hysteresis, including the endpoints.
    if (charging ? (percent < previous && int(percent) >= int(previous) - 2)
                 : (percent > previous && int(percent) <= int(previous) + 2))
      return previous;
    return percent;
  }
  constexpr char icon(uint8_t percent, bool charging) {
    return charging ? (percent >= 100 ? ';' : '/') : '0' + (percent + 5) / 10;
  }

}  // namespace battery_characterization
