#include "ui/display/pages/dialogs/page_flight_summary.h"

#include "Arduino.h"
#include "hardware/buttons.h"
#include "instruments/gps.h"
#include "logbook/logbook_entry.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

bool PageFlightSummary::showing_ = false;

namespace {
  constexpr uint8_t MENU_INPUT_X = 74;
  constexpr uint8_t MENU_BACK_Y = 190;
  constexpr uint8_t DELETE_HOLD_COUNT = 5;

  String formatSummaryAltitude(float meters) {
    return formatAlt(static_cast<int32_t>(meters * 100), settings.units_alt, true);
  }

  String formatSummaryClimb(float metersPerSecond) {
    String formatted = formatClimbRate(static_cast<int32_t>(fabsf(metersPerSecond) * 100),
                                       settings.units_climb, true);
    if (formatted.length() > 0 && formatted[0] == '+') {
      formatted.setCharAt(0, ' ');
    }
    return formatted;
  }

  String formatSummaryClimbRange(const FlightStats& stats) {
    return formatSummaryClimb(stats.climb_max / 100.0f) + String((char)141) +
           formatSummaryClimb(stats.climb_min / 100.0f) + String((char)142);
  }

  void drawMetricRow(const String& label, const String& value, uint8_t y) {
    u8g2.setCursor(0, y);
    u8g2.print(label);
    u8g2.setCursor(96 - u8g2.getStrWidth(value.c_str()), y);
    u8g2.print(value);
  }

  void drawMetricRow(char labelGlyph, const String& value, uint8_t y) {
    u8g2.setCursor(0, y);
    u8g2.print(labelGlyph);
    u8g2.setCursor(96 - u8g2.getStrWidth(value.c_str()), y);
    u8g2.print(value);
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

  String formatMaxWind(const FlightStats& stats) {
    return formatWindSpeed(stats.windSpeedMax) + " " +
           formatWindDirection(stats.windDirectionFromAtMaxDeg);
  }

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
}  // namespace

PageFlightSummary::PageFlightSummary() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = 0;
}

void PageFlightSummary::draw() {
  u8g2.firstPage();
  do {
    display_menuTitle("SUMMARY");

    u8g2.setFont(leaf_6x12);
    if (deleted) {
      u8g2.setCursor(0, 67);
      u8g2.print("Log deleted");
    } else {
      u8g2.setFont(leaf_5x8);
      u8g2.setCursor(0, 35);
      u8g2.print(dateString(summary));
      const String time = timeString(summary);
      u8g2.setCursor(96 - u8g2.getStrWidth(time.c_str()), 35);
      u8g2.print(time);

      u8g2.setFont(leaf_6x12);
      drawMetricRow("Timer:", formatSeconds(stats.duration, false, 0), 50);
      drawMetricRow("MaxAlt:", formatSummaryAltitude(stats.gpsalt_max), 65);
      drawMetricRow("AbvTO:", formatSummaryAltitude(stats.gpsalt_above_launch_max), 78);
      drawMetricRow("", formatSummaryClimbRange(stats), 91);
      drawMetricRow("MaxSpeed:", formatSpeed(stats.speed_max, settings.units_speed, true), 104);
      drawMetricRow(
          "Accel:", formatAccel(stats.accel_max, true) + '/' + formatAccel(stats.accel_min, true),
          117);
      if (stats.windValid) {
        drawMetricRow((char)143, formatMaxWind(stats), 130);
      }

      if (cursor_position == 0) {
        u8g2.drawRBox(0, 153 - 13, 96, 15, 2);
        u8g2.setDrawColor(0);
      }
      u8g2.setCursor(2, 153);
      u8g2.print("Delete");
      u8g2.setCursor(cursor_position == 0 ? 65 : 80, 153);
      if (cursor_position == 0) {
        u8g2.print("HOLD");
      } else {
        u8g2.print((char)126);
      }
      u8g2.setDrawColor(1);
    }

    if (deletePending) {
      uint8_t width =
          deletePending >= DELETE_HOLD_COUNT ? 96 : deletePending * 96 / DELETE_HOLD_COUNT;
      u8g2.drawBox(0, 170, width, 4);
    }

    u8g2.setFont(leaf_6x12);
    if (cursor_position == CURSOR_BACK) {
      u8g2.drawRBox(MENU_INPUT_X - 10, MENU_BACK_Y - 14, 34, 16, 2);
    }
    u8g2.setCursor(2, MENU_BACK_Y);
    u8g2.print("Back");
    u8g2.setCursor(MENU_INPUT_X, MENU_BACK_Y);
    u8g2.setDrawColor(cursor_position == CURSOR_BACK ? 0 : 1);
    u8g2.print((char)124);
    u8g2.setDrawColor(1);
  } while (u8g2.nextPage());
}

void PageFlightSummary::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == CURSOR_BACK) {
    if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
      speaker.playSound(state == ButtonEvent::HELD ? fx::exit : fx::cancel);
      pop_page();
    }
    return;
  }

  if (cursor_position == 0 && !deleted) {
    if (state == ButtonEvent::INCREMENTED) {
      deletePending = count;
      if (count >= DELETE_HOLD_COUNT) {
        buttons.consumeButton();
        deleted = LogbookEntryFile::deleteFiles(logbookPath, trackPath);
        speaker.playSound(deleted ? fx::confirm : fx::bad);
        deletePending = 0;
      }
    } else if (state == ButtonEvent::RELEASED || state == ButtonEvent::CLICKED) {
      deletePending = 0;
    }
  }
}
