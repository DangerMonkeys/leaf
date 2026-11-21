#pragma once
#include <string>
class Navigator;
namespace sim {
  bool routeGuidanceValid(const std::string& preset, const std::string& policy);
  bool configureRouteGuidance(const std::string& preset, const std::string& policy);
  void disableRouteGuidance();
  bool routeGuidanceExperimentActive();
  void serviceRouteGuidance();
  bool updateRouteGuidance(Navigator& nav, double& bearingDeg);
  bool routeGuidanceDistance(double& distanceM);
  bool routeGuidanceCylinderDistance(double& distanceM, double* initialLegM = nullptr);
  bool routeGuidanceFollowingTurn(double& turnDeg);
  std::string routeGuidanceJson();
  std::string routeGuidanceLog();
}  // namespace sim
