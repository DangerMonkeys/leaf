#pragma once

#include <Arduino.h>
#include <Print.h>

#include "diagnostics/flight_diagnostics.h"

// Measure the actual SD-backed Print calls, including automatic stdio buffer writes.
// Recording an event only touches RAM; it cannot recursively write to this output.
class FlightDiagnosticOutput : public Print {
 public:
  explicit FlightDiagnosticOutput(Print& output) : output_(output) {}
  using Print::write;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    const uint32_t started = millis();
    const size_t written = output_.write(data, size);
    const uint32_t duration = uint32_t(millis() - started);
    if (duration >= FlightDiagnostics::SLOW_WRITE_MS)
      flightDiagnostics.record(FlightDiagnostics::Kind::SlowWrite, started, duration);
    return written;
  }

 private:
  Print& output_;
};
