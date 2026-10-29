#include <assert.h>
#include <stdio.h>
#include <initializer_list>
#include "training/instructor_battery.h"
using namespace training;
int main() {
  InstructorBattery b;
  assert(b.due(0) && !b.empty());
  b.sample(100, 2377, true, false, true, 0, true, false);
  assert(b.valid && b.millivolts == 4000 && b.percent == 90 && !b.charging && !b.usb);
  assert(!b.due(1099) && b.due(1100));
  b.sample(1100, 2377, true, true, true, 500, true, true);
  assert(b.charging && b.usb && b.percent == 74 && b.icon() == '/');
  b.sample(2100, 2496, true, true, false, 0, false, false);
  assert(b.percent == 100 && b.icon() == ';');
  b.sample(3100, 1903, true, true, false, 0, false, false);
  assert(b.millivolts > 3200 && b.percent == 0 && !b.empty()); // Never shut down on compensated volts.
  b.sample(4100, 1902, true, false, false, 0, false, false);
  assert(b.empty() && b.icon() == '0');
  b.sample(5100, 2377, false, true, false, 0, false, true);
  assert(!b.chargingKnown && !b.charging && !b.usbKnown && !b.usb && !b.empty());
  b.sample(0xfffffff0, 2377, true, false, false, 0, false, false);
  assert(!b.due(0x3d7) && b.due(0x3d8));
  // Ensure extracting the shared percentage code preserves production behavior at all voltages.
  for (unsigned mv = 2800; mv <= 4500; ++mv)
    for (bool charge : {false, true})
      for (bool sense : {false, true})
        for (unsigned previous = 0; previous <= 100; ++previous) {
          const auto adjusted = battery_characterization::voltageForPercent(mv, charge, sense, 500);
          unsigned reference = adjusted < 3250 ? 0 : adjusted > 4080 ? 100
                                                 : 100 * (adjusted - 3250) / (4080 - 3250);
          if (charge ? (reference < previous && int(reference) >= int(previous) - 2)
                     : (reference > previous && reference <= previous + 2))
            reference = previous;
          assert(battery_characterization::percentForReading(mv, charge, sense, 500, previous) == reference);
        }
  puts("Instructor battery: ADC scaling, compensated percentage, legacy/current-sense boards, hysteresis, charging icons, raw-voltage shutdown and 1 Hz rollover passed");
}
