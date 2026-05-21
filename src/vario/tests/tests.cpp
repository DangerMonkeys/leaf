#include "tests/gps_tests.h"

#include <Arduino.h>
#include "etl/message_bus.h"

void run_tests(etl::imessage_bus* bus) {
  test_gps_parsing(bus);
  while (true) {
    Serial.println("All tests were successful.");
    delay(5000);
  }
}
