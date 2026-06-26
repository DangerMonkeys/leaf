#include "ui/display/pages/menu/page_logbook.h"

#include "hardware/buttons.h"
#include "instruments/gps.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

namespace {
  constexpr uint8_t MENU_INPUT_X = 74;
  constexpr uint8_t MENU_BACK_Y = 190;
  constexpr uint8_t DELETE_HOLD_COUNT = 5;

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

  String formatMaxWind(const LogbookEntrySummary& summary) {
    if (!summary.maxWindValid) return "--";
    return formatWindSpeed(summary.maxWindSpeedMps) + " " +
           formatWindDirection(summary.maxWindDirectionFromDeg);
  }
}  // namespace

PageLogbook::PageLogbook() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = cursor_page;
}

void PageLogbook::showNewest() {
  cursor_position = CURSOR_BACK;
  deletePending = 0;
  loadNewest();
}

void PageLogbook::loadNewest() {
  String path;
  if (!LogbookStore::newestEntryPath(path)) {
    currentPath = "";
    summary = LogbookEntrySummary();
    position = 0;
    total = 0;
    cursor_max = CURSOR_BACK;
    cursor_position = CURSOR_BACK;
    return;
  }
  loadPath(path);
}

void PageLogbook::loadPath(const String& path) {
  currentPath = LogbookStore::normalizePath(path);
  deletePending = 0;
  if (!LogbookStore::readSummary(currentPath, summary)) {
    summary = LogbookEntrySummary();
    position = 0;
    total = LogbookStore::count();
    cursor_max = CURSOR_BACK;
    cursor_position = CURSOR_BACK;
    return;
  }
  cursor_max = cursor_page;
  LogbookStore::entryPositionNewestFirst(currentPath, position, total);
}

void PageLogbook::loadAdjacent(bool newer) {
  if (!summary.valid) return;

  String path;
  const bool found = newer ? LogbookStore::nextEntryPath(currentPath, path)
                           : LogbookStore::previousEntryPath(currentPath, path);
  if (found) {
    speaker.playSound(newer ? fx::increase : fx::decrease);
    loadPath(path);
  } else {
    speaker.playSound(fx::cancel);
  }
}

void PageLogbook::deleteCurrent() {
  if (!summary.valid) return;

  String replacementPath;
  if (!LogbookStore::previousEntryPath(currentPath, replacementPath)) {
    LogbookStore::nextEntryPath(currentPath, replacementPath);
  }

  if (LogbookStore::deleteEntry(currentPath)) {
    buttons.consumeButton();
    speaker.playSound(fx::confirm);
    if (replacementPath.isEmpty()) {
      loadNewest();
    } else {
      loadPath(replacementPath);
    }
  } else {
    deletePending = 0;
    speaker.playSound(fx::bad);
  }
}

void PageLogbook::draw() {
  u8g2.firstPage();
  do {
    display_menuTitle("LOGBOOK");
    if (summary.valid) {
      drawEntry();
    } else {
      drawEmpty();
    }
    drawBackRow();
  } while (u8g2.nextPage());
}

void PageLogbook::drawEmpty() {
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 67);
  u8g2.print("No flights");
}

void PageLogbook::drawEntry() {
  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(0, 35);
  u8g2.print(dateString());
  const String time = timeString();
  u8g2.setCursor(96 - u8g2.getStrWidth(time.c_str()), 35);
  u8g2.print(time);

  u8g2.setFont(leaf_6x12);
  drawMetricRow("Timer:", formatSeconds(summary.durationSeconds, false, 0), 50);

  drawMetricRow("MaxAlt:", formatLogbookAltitude(summary.maxAltitudeM), 65);
  drawMetricRow("AbvTO:", formatLogbookAltitude(summary.maxAltitudeAboveLaunchM), 78);
  drawMetricRow("", formatLogbookClimbRange(summary), 91);
  drawMetricRow("MaxSpeed:", formatSpeed(summary.maxGroundSpeedMps, settings.units_speed, true),
                104);
  drawMetricRow(
      "Accel:", formatAccel(summary.maxAccelG, true) + '/' + formatAccel(summary.minAccelG, true),
      117);
  if (summary.maxWindValid) {
    drawMetricRow((char)143, formatMaxWind(summary), 130);
  }

  drawDeleteRow(153);
  drawPageRow(168);

  if (deletePending) {
    uint8_t width =
        deletePending >= DELETE_HOLD_COUNT ? 96 : deletePending * 96 / DELETE_HOLD_COUNT;
    u8g2.drawBox(0, 170, width, 4);
  }
}

void PageLogbook::drawBackRow() {
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
}

void PageLogbook::drawDeleteRow(uint8_t y) {
  if (cursor_position == cursor_delete) {
    u8g2.drawRBox(0, y - 13, 96, 15, 2);
    u8g2.setDrawColor(0);
  }

  u8g2.setCursor(2, y);
  u8g2.print("Delete");
  u8g2.setCursor(cursor_position == cursor_delete ? 65 : 80, y);
  if (cursor_position == cursor_delete) {
    u8g2.print("HOLD");
  } else {
    u8g2.print((char)126);
  }
  u8g2.setDrawColor(1);
}

void PageLogbook::drawPageRow(uint8_t y) {
  if (cursor_position == cursor_page) {
    u8g2.drawRBox(0, y - 13, 96, 15, 2);
    u8g2.setDrawColor(0);
  }

  if (position > 1) {
    u8g2.setCursor(2, y);
    u8g2.print((char)140);
  }

  String pageText = String(position) + "/" + String(total);
  u8g2.setCursor(48 - (pageText.length() * 3), y);
  u8g2.print(pageText);

  if (position < total) {
    u8g2.setCursor(80, y);
    u8g2.print((char)126);
  }

  u8g2.setDrawColor(1);
}

String PageLogbook::dateString() const {
  if (summary.startTimeValid && summary.startTimeLocal.length() >= 10) {
    return summary.startTimeLocal.substring(5, 7) + "/" + summary.startTimeLocal.substring(8, 10) +
           "/" + summary.startTimeLocal.substring(2, 4);
  }
  return "Date?";
}

String PageLogbook::timeString() const {
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

void PageLogbook::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == cursor_back) {
    if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
      speaker.playSound(state == ButtonEvent::HELD ? fx::exit : fx::cancel);
      logMenuPage.backToLogMenu();
      if (state == ButtonEvent::HELD) {
        mainMenuPage.backToMainMenu();
      }
    }
    return;
  }

  if (cursor_position == cursor_page) {
    if (state == ButtonEvent::CLICKED) {
      if (dir == Button::LEFT) {
        loadAdjacent(true);
      } else if (dir == Button::RIGHT) {
        loadAdjacent(false);
      }
    }
    return;
  }

  if (cursor_position == cursor_delete) {
    if (state == ButtonEvent::INCREMENTED) {
      deletePending = count;
      if (count >= DELETE_HOLD_COUNT) {
        deleteCurrent();
      }
    } else if (state == ButtonEvent::RELEASED || state == ButtonEvent::CLICKED) {
      deletePending = 0;
    }
  }
}
