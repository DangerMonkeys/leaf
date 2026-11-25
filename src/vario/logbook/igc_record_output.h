#pragma once

#include <MD5.h>
#include <ctype.h>

#include "logging/flight_diagnostic_output.h"

// IgcLogger v0.0.6 cannot write J/K records. Hash the combined output here so records
// from the library and Leaf's standard J/K writer share the same canonical-LF checksum.
class IgcRecordOutput : public FlightDiagnosticOutput {
 public:
  explicit IgcRecordOutput(Print& output) : FlightDiagnosticOutput(output) {}
  using Print::write;
  void reset() { checksum_.Reset(); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    const size_t written = FlightDiagnosticOutput::write(data, size);
    size_t start = 0;
    for (size_t i = 0; i < written; ++i) {
      if (data[i] != '\r') continue;
      if (i > start) checksum_.Update(data + start, i - start);
      start = i + 1;
    }
    if (written > start) checksum_.Update(data + start, written - start);
    return written;
  }
  void writeGRecord() {
    uint8_t raw[MD5::HASH_SIZE];
    checksum_.Finalize(raw);
    char line[MD5::DIGEST_SIZE + 4] = "G";
    MD5::Digest(raw, line + 1);
    for (size_t i = 1; i <= MD5::DIGEST_SIZE; ++i) line[i] = toupper(line[i]);
    line[MD5::DIGEST_SIZE + 1] = '\r';
    line[MD5::DIGEST_SIZE + 2] = '\n';
    // Bypass our hash update: the final G record is not part of its own checksum.
    FlightDiagnosticOutput::write(reinterpret_cast<const uint8_t*>(line), MD5::DIGEST_SIZE + 3);
  }

 private:
  MD5 checksum_;
};
