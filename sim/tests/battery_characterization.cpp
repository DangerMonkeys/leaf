#include "battery_characterization.h"

using namespace battery_characterization;

int main() {
  // Measured-current hardware: I*R correction matches the drop when charging stops.
  static_assert(voltageForPercent(4000, true, true, 500) == 3870);
  static_assert(voltageForPercent(3870, false, true, 0) == 3870);
  static_assert(voltageForPercent(4000, true, true, 0) == 4000);
  static_assert(voltageForPercent(4000, true, true, 2000) == 3790);  // 810mA cap
  static_assert(voltageForPercent(3680, true, true, 410) == 3574);  // observed ~52% -> ~39%

  // Older hardware: 410mA below 4V, linear taper to zero at 4.2V.
  static_assert(estimatedChargeCurrentMA(3900, false, 0) == 410);
  static_assert(estimatedChargeCurrentMA(4000, false, 0) == 410);
  static_assert(estimatedChargeCurrentMA(4100, false, 0) == 205);
  static_assert(estimatedChargeCurrentMA(4200, false, 0) == 0);
  static_assert(voltageForPercent(4000, true, false, 0) == 3894);
  static_assert(voltageForPercent(4100, true, false, 0) == 4047);
  static_assert(voltageForPercent(4200, true, false, 0) == 4200);
  static_assert(voltageForPercent(50, true, false, 0) == 0);  // saturates, never underflows
  static_assert(voltageForPercent(3894, false, false, 0) == 3894);
}
