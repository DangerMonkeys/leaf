#include "igc.h"

#include <SD_MMC.h>
#include <string_view>

#include "Arduino.h"
#include "ArduinoJson.h"
#include "FS.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
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

  uint32_t takeUtf8Character(std::string_view& text) {
    constexpr uint32_t REPLACEMENT = 0xFFFD;
    const auto lead = static_cast<uint8_t>(text.front());
    text.remove_prefix(1);
    if (lead < 0x80) return lead;
    if (lead < 0xC2 || lead > 0xF4) return REPLACEMENT;

    const unsigned continuationCount = lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
    uint32_t character = lead & (0x3F >> continuationCount);
    for (unsigned i = 0; i < continuationCount; ++i) {
      if (text.empty()) return REPLACEMENT;
      const auto next = static_cast<uint8_t>(text.front());
      if ((next & 0xC0) != 0x80) return REPLACEMENT;
      character = (character << 6) | (next & 0x3F);
      text.remove_prefix(1);
    }
    if ((continuationCount == 2 && character < 0x800) ||
        (continuationCount == 3 && character < 0x10000) ||
        (character >= 0xD800 && character <= 0xDFFF) || character > 0x10FFFF)
      return REPLACEMENT;
    return character;
  }

  std::string_view igcTransliteration(uint32_t character) {
    switch (character) {
      case 0x00C6:
        return "AE";
      case 0x00DE:
        return "TH";
      case 0x00DF:
        return "ss";
      case 0x00E6:
        return "ae";
      case 0x00FE:
        return "th";
      case 0x0141:
        return "L";
      case 0x0142:
        return "l";
      case 0x0152:
        return "OE";
      case 0x0153:
        return "oe";
      case 0x0160:
        return "S";
      case 0x0161:
        return "s";
      case 0x0178:
        return "Y";
      case 0x017D:
        return "Z";
      case 0x017E:
        return "z";
      case 0x2010:
      case 0x2011:
      case 0x2012:
      case 0x2013:
      case 0x2014:
        return "-";
      case 0x2018:
      case 0x2019:
        return "'";
      case 0x201C:
      case 0x201D:
        return "\"";
      default:
        return {};
    }
  }

  String igcText(const String& value, size_t maxLength) {
    // ASCII equivalents in code point order from U+00C0 through U+00FF.
    static constexpr char LATIN1[] =
        "AAAAAA CEEEEIIII"
        "DNOOOOOxOUUUUY  "
        "aaaaaa ceeeeiiii"
        "dnooooo/ouuuuy y";
    static_assert(sizeof(LATIN1) == 65);
    constexpr std::string_view RESERVED = "!$*\\^";
    String safe;
    safe.reserve(min(size_t(value.length()), maxLength));
    std::string_view text(value.c_str(), value.length());
    while (!text.empty() && safe.length() < maxLength) {
      const uint32_t character = takeUtf8Character(text);
      if (character >= 0x20 && character <= 0x7D &&
          RESERVED.find(static_cast<char>(character)) == std::string_view::npos) {
        safe += static_cast<char>(character);
        continue;
      }
      // Drop combining accents so decomposed names also keep their base letters.
      if (character >= 0x0300 && character <= 0x036F) continue;

      const auto replacement = igcTransliteration(character);
      if (!replacement.empty()) {
        if (replacement.size() > maxLength - safe.length()) break;
        for (char c : replacement) safe += c;
      } else {
        safe += (character >= 0x00C0 && character <= 0x00FF) ? LATIN1[character - 0x00C0] : ' ';
      }
    }
    safe.trim();
    return safe;
  }

  String pointLabel(const Waypoint& waypoint) {
    return igcText(String(waypoint.name), maxGpxNameLength);
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
    if (!baro.climbRate1SecAverageValid()) return "000";

    const int32_t climbRateCms = baro.climbRate1SecAverage();
    const int32_t climbTenthsMps =
        climbRateCms >= 0 ? (climbRateCms + 5) / 10 : -((-climbRateCms + 5) / 10);
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
    return igcText(description, 58);
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
    return igcText(comment, 72);
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
  const bool writeFix = gps.hasUsableFix() && baro.state() == Barometer::State::Ready;
  if (writeFix) {
    logSensorClock();
    logger.writeBRecord(utcTimeString(),  // Time in HHMMSS
                        latDegreeToStr(gps.location.lat()), lngDegreeToStr(gps.location.lng()),
                        true,
                        baro.alt() / 100,  // cm to meters
                        gps.altitude.meters(), igcBRecordExtensions());
  }
  logSensors(writeFix);
  logDiagnostics();
}

void Igc::logSensors(bool writeSample) {
  sensorLog_.update(imu.gLoad().logState(millis()), millis(), writeSample,
                    [this](const char* line) { logger.writeLRecord(line); });
  if (!writeSample) return;
  const bool ambientValid = ambient.state() == Ambient::State::Ready;
  const String utc = utcTimeString();
  ambientLog_.update(millis(), utc.c_str(), ambientValid, ambientValid ? ambient.temp() : 0,
                     ambientValid ? ambient.humidity() : 0,
                     [this](const char* line) { recordOutput_.println(line); });
}

void Igc::logSensorClock() {
  // Anchor sensor uptimes to the same GPS clock as B records, including centiseconds.
  // Refresh hourly or after a clock correction, rather than adding a clock line per fix.
  const uint32_t receivedMs = millis() - gps.time.age();
  const uint32_t utcMs =
      ((gps.time.hour() * 60UL + gps.time.minute()) * 60 + gps.time.second()) * 1000 +
      gps.time.centisecond() * 10;
  const uint32_t elapsed = receivedMs - sensorClockMs_;
  const uint32_t predicted = (sensorClockUtcMs_ + elapsed) % 86400000;
  int32_t difference = static_cast<int32_t>(utcMs) - static_cast<int32_t>(predicted);
  if (difference > 43200000) difference -= 86400000;
  if (difference < -43200000) difference += 86400000;
  if (sensorClockValid_ && elapsed < 3600000 && abs(difference) <= 2000) return;
  char line[40];
  const String date = utcDateString();
  const String utc = utcTimeString();
  snprintf(line, sizeof(line), "A%08lX%s%s%02u", (unsigned long)receivedMs, date.c_str(),
           utc.c_str(), gps.time.centisecond());
  logger.writeLRecord(line);
  sensorClockValid_ = true;
  sensorClockMs_ = receivedMs;
  sensorClockUtcMs_ = utcMs;
}

bool Igc::startFlight() {
  auto success = Flight::startFlight();
  if (!success) return false;

  recordOutput_.reset();
  logger.setOutput(recordOutput_);
  sensorLog_.reset();
  ambientLog_.reset();
  sensorClockValid_ = false;

  // Log the Header
  // A record to look like "AXLVLeaf1"
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

  // Arduino String keeps its capacity when assigned "". A null C string frees it.
  logger.pilot = static_cast<const char*>(nullptr);
  logger.glider_type = static_cast<const char*>(nullptr);
  logger.firmware_version = static_cast<const char*>(nullptr);
  logger.hardware_version = static_cast<const char*>(nullptr);
  logger.logger_type = static_cast<const char*>(nullptr);
  logger.gps_type = static_cast<const char*>(nullptr);
  logger.pressure_type = static_cast<const char*>(nullptr);
  logger.time_zone = static_cast<const char*>(nullptr);

  // Log the I record for the extension values appended to each B fix record.
  const IRecordExtension extensions[] = {IRecordExtension(3, "FXA"), IRecordExtension(3, "GSP"),
                                         IRecordExtension(3, "TRT"), IRecordExtension(3, "WDI"),
                                         IRecordExtension(3, "WSP"), IRecordExtension(3, "VAR")};
  logger.writeIRecord(sizeof(extensions) / sizeof(extensions[0]), extensions);
  recordOutput_.println(IgcAmbientLog::DECLARATION);
  char config[73];
  snprintf(config, sizeof(config), "FDCFG V=1 WRITE_MS=%lu GAP_MS=%lu REPORT_MS=%lu",
           (unsigned long)FlightDiagnostics::SLOW_WRITE_MS,
           (unsigned long)FlightDiagnostics::GAP_MS, (unsigned long)FlightDiagnostics::REPORT_MS);
  logger.writeLRecord(config);
  logger.writeLRecord("SENSOR V=2 G=MG HEX=1");
  writeActiveNavigationDeclaration();
  log_writeStartDiagnostics(logger);

  return true;
}

void Igc::logDiagnostics(bool final) {
  if (!started()) return;
  bool clockWritten = false;
  flightDiagnostics.report(
      millis(),
      [this, &clockWritten](const char* line) {
        if (!clockWritten) {
          // Anchor original event uptimes to UTC without restoring a per-second record.
          char clock[48];
          const String utc = utcTimeString();
          snprintf(clock, sizeof(clock), "FDTIME T=%lu UTC=%s", (unsigned long)millis(),
                   utc.c_str());
          logger.writeLRecord(clock);
          clockWritten = true;
        }
        logger.writeLRecord(line);
      },
      final);
}

void Igc::markEnd(const char* cause) {
  if (!started()) return;
  char line[64];
  snprintf(line, sizeof(line), "END V=1 CAUSE=%s T_MS=%lu", cause,
           static_cast<unsigned long>(millis()));
  logger.writeLRecord(line);  // Included in the normal G-record checksum at close.
}

void Igc::end(const FlightStats stats, bool showSummary) {
  // If we've not started a flight yet, don't write to disk.
  if (started()) {
    logDiagnostics(true);
    recordOutput_.writeGRecord();
  }
  Flight::end(stats, showSummary);
}

void Igc::setPilotFromProfiles() {
  PilotProfile pilot;
  if (ProfileStore::activePilot(pilot)) {
    const String name = igcText(pilot.name, pilot.name.length());
    if (!name.isEmpty()) logger.pilot = name;
  }

  GliderProfile glider;
  if (ProfileStore::activeGlider(glider)) {
    const String profileName = glider.profileName();
    const String gliderType = igcText(profileName, profileName.length());
    if (!gliderType.isEmpty()) logger.glider_type = gliderType;
  }
}

void Igc::markSavedPoint(const Waypoint& waypoint) {
  if (!started()) return;
  logger.writeERecord(utcTimeString(), "PEV", igcText(String("Save Point ") + waypoint.name, 66));
}

void Igc::writeActiveNavigationDeclaration() {
  const RouteID routeIndex = navigator.routeContextIndex();
  if (routeIndex) {
    const Route& route = navigator.routes[routeIndex];
    if (!route.totalPoints) return;

    const uint8_t turnpointCount = route.totalPoints > 2 ? route.totalPoints - 2 : 0;
    const String routeName = igcText(String(route.name), maxGpxNameLength);
    Serial.print("IGC task declaration route: ");
    Serial.println(routeName);
    logger.writeCDeclarationRecord(utcDateString(), utcTimeString(), utcDateString(), "0000",
                                   turnpointCount, igcText(String("Leaf Route ") + routeName, 51));
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
    logger.writeLRecord(igcText(String("NAV:ROUTE NAME=") + routeName, 72));

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
                                 igcText(String("Leaf Point ") + pointName, 51));
  writeTaskPlaceholders(logger, true);
  logger.writeCPointRecord(latDegreeToStr(point.latitude()), lngDegreeToStr(point.longitude()),
                           cPointAreaDescription("START", point, defaultWaypointRadius));
  logger.writeCPointRecord(latDegreeToStr(point.latitude()), lngDegreeToStr(point.longitude()),
                           cPointAreaDescription("FINISH", point, defaultWaypointRadius));
  writeTaskPlaceholders(logger, false);
  logger.writeLRecord(
      igcText(String("NAV:POINT R=") + defaultWaypointRadius + " NAME=" + pointName, 72));
}
