#include "igc.h"

#include <SD_MMC.h>

#include "Arduino.h"
#include "ArduinoJson.h"
#include "FS.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "navigation/gpx.h"
#include "profiles/profile_store.h"
#include "system/version_info.h"
#include "time.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"
#include "wind_estimate/wind_estimate.h"

namespace {
  String utcTimeString() {
    char buf[8];
    tm cal;
    gps.getUtcDateTime(cal);
    strftime(buf, sizeof(buf), "%H%M%S", &cal);
    return String(buf);
  }

  String utcDateString() {
    char buf[8];
    tm cal;
    gps.getUtcDateTime(cal);
    strftime(buf, sizeof(buf), "%d%m%y", &cal);
    return String(buf);
  }

  String printableAscii(const String& value, uint8_t maxLength) {
    String safe;
    safe.reserve(min((uint16_t)value.length(), (uint16_t)maxLength));
    for (uint16_t i = 0; i < value.length() && safe.length() < maxLength; i++) {
      char c = value[i];
      safe += (c >= 0x20 && c <= 0x7E) ? c : ' ';
    }
    safe.trim();
    return safe;
  }

  String pointLabel(const Waypoint& waypoint) {
    return printableAscii(String(waypoint.name), maxGpxNameLength);
  }

  const char* routeRoleName(RoutePointRole role) {
    switch (role) {
      case RoutePointRole::Takeoff:
        return "TAKEOFF";
      case RoutePointRole::StartSpeedSection:
        return "SSS";
      case RoutePointRole::EndSpeedSection:
        return "ESS";
      case RoutePointRole::EndSpeedSectionGoal:
        return "ESS_GOAL";
      case RoutePointRole::Goal:
        return "GOAL";
      case RoutePointRole::Normal:
      default:
        return "TURN";
    }
  }

  const char* cPointRoleName(RoutePointRole role, RouteIndex pointIndex, uint8_t totalPoints) {
    switch (role) {
      case RoutePointRole::Takeoff:
        return "TAKEOFF";
      case RoutePointRole::StartSpeedSection:
        return "START";
      case RoutePointRole::EndSpeedSection:
        return "ESS";
      case RoutePointRole::EndSpeedSectionGoal:
      case RoutePointRole::Goal:
        return "FINISH";
      case RoutePointRole::Normal:
      default:
        if (pointIndex == 1) return "START";
        if (pointIndex == totalPoints) return "FINISH";
        return "TURN";
    }
  }

  String areaDistanceField(uint16_t radiusM) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%07lu", static_cast<unsigned long>(radiusM));
    return String(buf);
  }

  String fixedWidthUnsigned(float value, uint16_t maxValue) {
    const uint16_t rounded = constrain(static_cast<int>(value + 0.5f), 0, maxValue);
    char buf[4];
    snprintf(buf, sizeof(buf), "%03u", rounded);
    return String(buf);
  }

  String fixedWidthDegrees(float degrees) {
    int rounded = static_cast<int>(degrees + 0.5f) % 360;
    if (rounded < 0) rounded += 360;
    return fixedWidthUnsigned(rounded, 359);
  }

  String varioField() {
    if (!baro.climbRateFilteredValid()) return "000";

    const int32_t climbTenthsMps = baro.climbRateFiltered() >= 0
                                       ? (baro.climbRateFiltered() + 5) / 10
                                       : -((-baro.climbRateFiltered() + 5) / 10);
    char buf[5];
    if (climbTenthsMps >= 0) {
      snprintf(buf, sizeof(buf), "%03ld", constrain(climbTenthsMps, 0L, 999L));
    } else {
      snprintf(buf, sizeof(buf), "-%02ld", constrain(-climbTenthsMps, 0L, 99L));
    }
    return String(buf);
  }

  String igcBRecordExtensions() {
    const WindEstimate& windEstimate = windEstimator.getWindEstimate();
    String extensions = toDigits((int)gps.fixInfo.error, 3);
    extensions += fixedWidthUnsigned(gps.speed.isValid() ? gps.speed.kmph() : 0, 999);
    extensions += fixedWidthDegrees(gps.course.isValid() ? gps.course.deg() : 0);
    extensions += fixedWidthDegrees(
        windEstimate.validEstimate ? windEstimate.windDirectionFrom * RAD_TO_DEG : 0);
    extensions +=
        fixedWidthUnsigned(windEstimate.validEstimate ? windEstimate.windSpeed * 3.6f : 0, 999);
    extensions += varioField();
    return extensions;
  }

  String cPointAreaDescription(const char* role, const Waypoint& waypoint, uint16_t radiusM) {
    const bool isStart = strcmp(role, "START") == 0;
    String description = isStart ? areaDistanceField(radiusM) : String("0000000");
    description += isStart ? "9999999" : areaDistanceField(radiusM);
    description += "000000360000";
    description += role;
    description += "AREA ";
    description += pointLabel(waypoint);
    return printableAscii(description, 58);
  }

  String indexedLabel(uint8_t index, uint8_t total) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u/%02u", index, total);
    return String(buf);
  }

  String taskPointMetadata(uint8_t index, uint8_t total, const RoutePoint& meta,
                           const Waypoint& waypoint) {
    String comment = "NAVTP:" + indexedLabel(index, total);
    comment += " R=";
    comment += meta.radiusM;
    comment += " ROLE=";
    comment += routeRoleName(meta.role);
    comment += " NAME=";
    comment += pointLabel(waypoint);
    return printableAscii(comment, 72);
  }

  void writeTaskPlaceholders(IgcLogger& logger, bool takeoff) {
    logger.writeCPointRecord("0000000N", "00000000E", takeoff ? "TAKEOFF" : "LANDING");
  }
}  // namespace

String latDegreeToStr(double degree) {
  char output[9];  // 8 bytes + null terminator
  char hemisphere = (degree >= 0) ? 'N' : 'S';
  degree = abs(degree);

  int degrees = (int)degree;
  double minutes = (degree - degrees) * 60;
  int intMinutes = (int)minutes;
  int fractionalMinutes = (int)((minutes - intMinutes) * 1000);

  snprintf(output, 9, "%02d%02d%03d%c", degrees, intMinutes, fractionalMinutes, hemisphere);
  return String(output);
}

String lngDegreeToStr(double degree) {
  char output[10];  // 9 bytes + null terminator
  char hemisphere = (degree >= 0) ? 'E' : 'W';
  degree = abs(degree);

  int degrees = (int)degree;
  double minutes = (degree - degrees) * 60;
  int intMinutes = (int)minutes;
  int fractionalMinutes = (int)((minutes - intMinutes) * 1000);

  snprintf(output, 10, "%03d%02d%03d%c", degrees, intMinutes, fractionalMinutes, hemisphere);
  return String(output);
}

const String Igc::desiredFileName() const {
  // Name of the file should be for example 2024-12-10-XFH-000-01.IGC
  // as per the IGC spec.
  char buf[11];
  tm cal;
  gps.getLocalDateTime(cal);
  strftime(buf, 11, "%F", &cal);

  String ret = buf;
  ret += (String) "-" + IGC_MANUFACTURER_CODE + "-000";
  return ret;
}

void Igc::log(unsigned long durationSec) {
  // Short-circuit if we've not yet started a flight
  if (!started()) return;
  if (!gps.hasUsableFix()) return;

  logger.writeBRecord(utcTimeString(),  // Time in HHMMSS
                      latDegreeToStr(gps.location.lat()), lngDegreeToStr(gps.location.lng()), true,
                      baro.alt() / 100,  // cm to meters
                      gps.altitude.meters(), igcBRecordExtensions());
}

bool Igc::startFlight() {
  auto success = Flight::startFlight();
  if (!success) return false;

  logger.setOutput(file);

  // Log the Header
  // A record to look like "AXLFLeaf1"
  logger.setManufacturerId(IGC_MANUFACTURER_CODE);
  logger.setLoggerId("Lea");
  logger.setIdExtension("f1");

  logger.pilot = "Unknown";
  logger.glider_type = "Unknown";
  setPilotFromProfiles();

  char firmwareVersion[48];
  char hardwareVersion[24];
  LeafVersionInfo::firmwareDisplayVersion(firmwareVersion, sizeof(firmwareVersion));
  LeafVersionInfo::hardwareDisplayVersion(hardwareVersion, sizeof(hardwareVersion));

  const String macAddress =
      settings.macAddress.isEmpty() ? settings.getMacAddress() : settings.macAddress;
  logger.firmware_version = firmwareVersion;
  logger.hardware_version = hardwareVersion;
  logger.logger_type = (String) "Leaf:" + macAddress;
  logger.gps_type = "GNSS LC86G";
  logger.pressure_type = "MS5611";
  logger.time_zone = (String)(settings.system_timeZone / 60);

  tm cal;
  gps.getUtcDateTime(cal);
  strftime(logger.date, sizeof(logger.date), "%d%m%y", &cal);

  logger.fix_accuracy = 2;  // Quectel LC86G spec: 2.0m CEP horizontal accuracy
  logger.writeHeader();

  // Log the I record for the extension values appended to each B fix record.
  const IRecordExtension extensions[] = {IRecordExtension(3, "FXA"), IRecordExtension(3, "GSP"),
                                         IRecordExtension(3, "TRT"), IRecordExtension(3, "WDI"),
                                         IRecordExtension(3, "WSP"), IRecordExtension(3, "VAR")};
  logger.writeIRecord(sizeof(extensions) / sizeof(extensions[0]), extensions);
  writeActiveNavigationDeclaration();

  return true;
}

void Igc::end(const FlightStats stats, bool showSummary) {
  // If we've not started a flight yet, don't write to disk.
  if (started()) {
    logger.writeGRecord();
  }
  Flight::end(stats, showSummary);
}

void Igc::setPilotFromProfiles() {
  PilotProfile pilot;
  if (ProfileStore::activePilot(pilot)) {
    logger.pilot = pilot.name;
  }

  GliderProfile glider;
  if (ProfileStore::activeGlider(glider)) {
    const String profileName = glider.profileName();
    if (!profileName.isEmpty()) logger.glider_type = profileName;
  }
}

void Igc::markSavedPoint(const Waypoint& waypoint) {
  if (!started()) return;
  logger.writeERecord(utcTimeString(), "PEV",
                      printableAscii(String("Save Point ") + waypoint.name, 66));
}

void Igc::writeActiveNavigationDeclaration() {
  const RouteID routeIndex = navigator.routeContextIndex();
  if (routeIndex) {
    const Route& route = navigator.routes[routeIndex];
    if (!route.totalPoints) return;

    const uint8_t turnpointCount = route.totalPoints > 2 ? route.totalPoints - 2 : 0;
    const String routeName = printableAscii(String(route.name), maxGpxNameLength);
    Serial.print("IGC task declaration route: ");
    Serial.println(routeName);
    logger.writeCDeclarationRecord(utcDateString(), utcTimeString(), utcDateString(), "0000",
                                   turnpointCount,
                                   printableAscii(String("Leaf Route ") + routeName, 51));
    writeTaskPlaceholders(logger, true);

    for (uint8_t i = 1; i <= route.totalPoints; i++) {
      const RouteIndex pointIndex(i);
      const RoutePoint& meta = navigator.routePointMeta(routeIndex, pointIndex);
      const Waypoint& point = navigator.routePoint(routeIndex, pointIndex);
      const char* role = cPointRoleName(meta.role, pointIndex, route.totalPoints);
      logger.writeCPointRecord(latDegreeToStr(point.latitude()), lngDegreeToStr(point.longitude()),
                               cPointAreaDescription(role, point, meta.radiusM));
    }

    writeTaskPlaceholders(logger, false);
    logger.writeLRecord(printableAscii(String("NAV:ROUTE NAME=") + routeName, 72));

    for (uint8_t i = 1; i <= route.totalPoints; i++) {
      const RouteIndex pointIndex(i);
      const RoutePoint& meta = navigator.routePointMeta(routeIndex, pointIndex);
      const Waypoint& point = navigator.routePoint(routeIndex, pointIndex);
      logger.writeLRecord(taskPointMetadata(i, route.totalPoints, meta, point));
    }
    return;
  }

  if (!navigator.activeWaypointIndex) return;

  const Waypoint& point = navigator.activePoint;
  const String pointName = pointLabel(point);
  Serial.print("IGC nav declaration point: ");
  Serial.println(pointName);
  logger.writeCDeclarationRecord(utcDateString(), utcTimeString(), utcDateString(), "0000", 0,
                                 printableAscii(String("Leaf Point ") + pointName, 51));
  writeTaskPlaceholders(logger, true);
  logger.writeCPointRecord(latDegreeToStr(point.latitude()), lngDegreeToStr(point.longitude()),
                           cPointAreaDescription("START", point, defaultWaypointRadius));
  logger.writeCPointRecord(latDegreeToStr(point.latitude()), lngDegreeToStr(point.longitude()),
                           cPointAreaDescription("FINISH", point, defaultWaypointRadius));
  writeTaskPlaceholders(logger, false);
  logger.writeLRecord(
      printableAscii(String("NAV:POINT R=") + defaultWaypointRadius + " NAME=" + pointName, 72));
}
