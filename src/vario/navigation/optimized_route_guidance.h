#pragma once
#include <Arduino.h>
#include <cstdint>
class Navigator;
namespace route_guidance {
  // All mutation/observation runs on the firmware loop thread.
  struct Solution {
    bool valid = false, hasFollowing = false;
    double goalM = 0, cylinderM = 0, initialLegM = 0, bearingDeg = 0, followingBearingDeg = 0;
    double targetLatitude = 0, targetLongitude = 0;
    float marginM = 0;
  };
  struct Diagnostics {
    uint32_t fixes = 0, iterations = 0, publications = 0, restarts = 0, fallbacks = 0;
    uint32_t maxIterationUs = 0, maxSliceUs = 0, maxUpdateUs = 0, maxFixWorkUs = 0;
    uint32_t lastFixWorkUs = 0, fixWorkUs = 0, maxPublicationLatencyMs = 0, skippedSlices = 0;
    uint32_t maxGeometryUs = 0, maxPublishPointUs = 0, maxRefreshUs = 0, slicesOverBudget = 0;
    uint32_t maxRequestLatencyMs = 0, lastRequestLatencyMs = 0, maxFirstPlanLatencyMs = 0;
    uint32_t planSourceAgeMs = 0;
    uint32_t workspaceBytes = 0;
    uint16_t pendingIterations = 0;
  };
  void invalidate();
  void update(const Navigator& nav);
  // One geometry point, solver iteration, or guidance point per call.
  bool hasPendingWork();
  void service(const Navigator& nav);
  void skippedSlice();
  const Solution* solution(const Navigator& nav);
  const Diagnostics& diagnostics();
  const char* status();
  void writeJson(Print& out);
}  // namespace route_guidance
