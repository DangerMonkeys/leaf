#include "tests/gps_tests.h"

#include <Arduino.h>
#include "etl/message_bus.h"

#include "dispatch/message_types.h"

void test_gps_parsing(etl::imessage_bus* bus) {
  GpsMessage msg = GpsMessage(
      NMEAString("$GPGSV,2,1,07,11,79,032,27,12,70,321,36,06,35,041,31,21,34,143,23,1*6F*38"));
  bus->receive(msg);
  Serial.println("GPS parsing test successful.");
}
