#include "ui/display/logbook_card.h"

#include "Arduino.h"
#include "instruments/gps.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

namespace {
  String formatLogbookAltitude(float meters) {
    return formatAlt(static_cast<int32_t>(meters * 100), settings.units_alt, true);
  }

  String formatLogbookClimb(float metersPerSecond) {
    String formatted = formatClimbRate(static_cast<int32_t>(fabsf(metersPerSecond) * 100),
                                       settings.units_climb, true);
    if (formatted.length() > 0 && formatted[0] == '+') {
      formatted.setCharAt(0, ' ');
    }
    return formatted;
  }

  String formatLogbookClimbWithArrow(float metersPerSecond, char arrow) {
    return formatLogbookClimb(metersPerSecond) + String(arrow);
  }

  String formatLogbookClimbRange(const LogbookEntrySummary& summary) {
    return formatLogbookClimbWithArrow(summary.maxClimbRateMps, (char)141) +
           formatLogbookClimbWithArrow(summary.maxSinkRateMps, (char)142);
  }

  String formatWindDirection(float directionFromDeg) {
    int degrees = static_cast<int>(directionFromDeg + 0.5f) % 360;
    if (settings.units_heading) {
      return gps.cardinal(degrees);
    }
    return String(degrees) + String((char)144);
  }

  String formatWindSpeed(float speedMps) {
    float speed = settings.units_speed ? speedMps * 2.23694f : speedMps * 3.6f;
    if (speed > 99) speed = 99;
    return String(static_cast<int>(speed + 0.5f)) + (settings.units_speed ? "mph" : "kph");
  }

  String formatMaxWind(const LogbookEntrySummary& summary) {
    if (!summary.maxWindValid) return "--";
    return formatWindSpeed(summary.maxWindSpeedMps) + " " +
           formatWindDirection(summary.maxWindDirectionFromDeg);
  }

  String formatLogbookDuration(uint32_t seconds) {
    if (seconds >= 3600) {
      return String(seconds / 3600) + "h" + toDigits((seconds % 3600) / 60, 2) + "m";
    }
    if (seconds >= 60) {
      return String(seconds / 60) + "m" + toDigits(seconds % 60, 2) + "s";
    }
    return String(seconds) + "s";
  }

  String formatLogbookAccelRange(const LogbookEntrySummary& summary) {
    return formatAccel(summary.minAccelG, false) + "/" + formatAccel(summary.maxAccelG, true);
  }

  constexpr uint8_t METRIC_LABEL_X = 2;
  constexpr uint8_t METRIC_VALUE_RIGHT_X = 94;
  constexpr uint8_t METRIC_FRAME_Y = 35;
  constexpr uint8_t METRIC_FRAME_H = 106;

  void drawMetricRow(const String& label, const String& value, uint8_t y) {
    u8g2.setCursor(METRIC_LABEL_X, y);
    u8g2.print(label);
    u8g2.setCursor(METRIC_VALUE_RIGHT_X - u8g2.getStrWidth(value.c_str()), y);
    u8g2.print(value);
  }

  void drawMetricRow(char labelGlyph, const String& value, uint8_t y) {
    u8g2.setCursor(METRIC_LABEL_X, y);
    u8g2.print(labelGlyph);
    u8g2.setCursor(METRIC_VALUE_RIGHT_X - u8g2.getStrWidth(value.c_str()), y);
    u8g2.print(value);
  }
}  // namespace

namespace logbook_card {
  String dateString(const LogbookEntrySummary& summary) {
    if (summary.startTimeValid && summary.startTimeLocal.length() >= 10) {
      return summary.startTimeLocal.substring(5, 7) + "/" +
             summary.startTimeLocal.substring(8, 10) + "/" + summary.startTimeLocal.substring(2, 4);
    }
    return "Date?";
  }

  String timeString(const LogbookEntrySummary& summary) {
    if (summary.startTimeValid && summary.startTimeLocal.length() >= 16) {
      int hour = summary.startTimeLocal.substring(11, 13).toInt();
      const String minute = summary.startTimeLocal.substring(14, 16);
      if (!settings.units_hours) {
        return summary.startTimeLocal.substring(11, 16);
      }
      const bool pm = hour >= 12;
      hour %= 12;
      if (hour == 0) hour = 12;
      return String(hour) + ":" + minute + (pm ? " PM" : " AM");
    }
    return "Time?";
  }

  void drawFlightCard(const LogbookEntrySummary& summary) {
    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(0, 25);
    u8g2.print(dateString(summary));
    const String time = timeString(summary);
    u8g2.setCursor(0, 34);
    u8g2.print(time);

    u8g2.setFont(leaf_6x12);
    const String duration = formatLogbookDuration(summary.durationSeconds);
    u8g2.setCursor(96 - u8g2.getStrWidth(duration.c_str()), 31);
    u8g2.print(duration);

    u8g2.drawRFrame(0, METRIC_FRAME_Y, 96, METRIC_FRAME_H, 3);

    drawMetricRow("MaxAlt:", formatLogbookAltitude(summary.maxAltitudeM), 49);
    drawMetricRow("", formatLogbookClimbRange(summary), 64);
    drawMetricRow(String((char)148) + "Dist:",
                  formatDistance(summary.straightLineDistanceM, settings.units_distance, true), 79);
    drawMetricRow(String((char)147) + "Path:",
                  formatDistance(summary.pathDistanceM, settings.units_distance, true), 94);
    drawMetricRow("MaxSpeed:", formatSpeed(summary.maxGroundSpeedMps, settings.units_speed, true),
                  109);
    drawMetricRow("Accel:", formatLogbookAccelRange(summary), 124);
    if (summary.maxWindValid) {
      drawMetricRow((char)143, formatMaxWind(summary), 139);
    }
  }
}  // namespace logbook_card
