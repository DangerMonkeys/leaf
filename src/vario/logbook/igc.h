#pragma once
#include "IgcLogger.h"
#include "flight.h"

// X for non-registered, LF for Leaf
#define IGC_MANUFACTURER_CODE "XLF"

class Igc : public Flight {
 public:
  bool startFlight() override;
  void end(const FlightStats stats) override;

  const String fileNameSuffix() const override { return "igc"; }

  const String desiredFileName() const override;
  void log(unsigned long durationSec) override;

 private:
  IgcLogger logger;
  void setPilotFromFile();
};