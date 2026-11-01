#pragma once
#include "IgcLogger.h"
#include "flight.h"
#include "logging/flight_diagnostic_output.h"

struct Waypoint;

// X for non-registered, LF for Leaf
#define IGC_MANUFACTURER_CODE "XLV"

class Igc : public Flight {
 public:
  bool startFlight() override;
  void end(const FlightStats stats, bool showSummary = true) override;

  const String fileNameSuffix() const override { return "igc"; }

  const String desiredFileName() const override;
  void log(unsigned long durationSec) override;

  void markSavedPoint(const Waypoint& waypoint);
  void markEnd(const char* cause);

 private:
  IgcLogger logger;
  FlightDiagnosticOutput diagnosticOutput_{file};
  void logDiagnostics(bool final = false);
  void setPilotFromProfiles();
  void writeActiveNavigationDeclaration();
};
