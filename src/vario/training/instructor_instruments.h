#pragma once
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "instructor_controls.h"
#include "instructor_ui.h"
#include "protocol.h"
#include "ui/display/thermal_page_fields.h"
namespace training {
  struct InstructorFlight {
    bool position = false, climb = false, altitude = false, barometric = false;
    int32_t altitudeUnits = 0;
    uint16_t speedUnits = 0, heading = 0;
    int16_t climbCms = 0;
  };
  inline InstructorFlight instructorFlight(const Receiver& receiver, uint32_t now,
                                           const InstructorUnits& units) {
    InstructorFlight value;
    const auto& s = receiver.students[receiver.focus];
    const auto& d = s.data;
    value.position = receiver.positionFresh(now);
    value.climb = receiver.climbFresh(now);
    value.climbCms = d.climbCms;
    const bool pressure = receiver.fresh(now) && (d.valid & Baro) &&
                          uint32_t(now - s.receivedAt) + d.pressureAge < LINK_STALE_MS;
    value.altitude = value.position || pressure;
    value.barometric = !value.position && pressure;
    // The transmitted barometric altitude is unadjusted: label BARO, never MSL.
    const int32_t cm = value.position ? d.gpsAltitudeCm : d.baroAltitudeCm;
    value.altitudeUnits = std::clamp<int64_t>(
        units.altitudeFeet ? int64_t(cm) * 100 / 3048 : cm / 100, -99999, 99999);
    value.speedUnits =
        (uint64_t(d.speedCms) * (units.speedMph ? 223694U : 360000U) + 5000000U) / 10000000U;
    value.heading = d.courseCdeg / 100 % 360;
    return value;
  }
  template <typename Display>
  void instructorNumber(Display& lcd, const char* text, uint8_t x, uint8_t y, uint8_t width,
                        const uint8_t* preferred) {
    lcd.setFont(preferred);
    if (lcd.getStrWidth(text) > width) lcd.setFont(leaf_8x14);
    if (lcd.getStrWidth(text) > width) lcd.setFont(leaf_6x12);
    if (lcd.getStrWidth(text) > width) lcd.setFont(leaf_5x8);
    lcd.drawStr(x + width - lcd.getStrWidth(text), y, text);
  }
  template <typename Display>
  void drawInstructorVario(Display& lcd, const InstructorFlight& value) {
    constexpr uint8_t top = thermal_page::VARIO_BAR_TOP + 2;
    constexpr uint8_t half = thermal_page::VARIO_BAR_HALF_HEIGHT;
    constexpr uint8_t width = thermal_page::CORE_VARIO_BAR_WIDTH;
    constexpr uint8_t middle = top + half;
    lcd.drawFrame(0, top, width, half * 2 + 1);
    int start = 0, end = -1;
    // Match Leaf's +/-5 m/s full half-bar and inverted 5..10 m/s extension.
    const int rate = value.climbCms;
    if (value.climb && rate >= -1000 && rate <= 1000) {
      if (rate > 500) {
        start = top + 1;
        end = middle - half * (rate - 500) / 500;
      } else if (rate < -500) {
        start = middle + half * (-rate - 500) / 500;
        end = middle + half - 1;
      } else if (rate < 0) {
        start = middle;
        end = middle + half * -rate / 500;
      } else {
        start = middle - half * rate / 500;
        end = middle;
      }
      if (end >= start) lcd.drawBox(1, start, width - 2, end - start + 1);
    }
    for (uint8_t i = 1; i < 10; ++i) {
      const uint8_t y = top + i * (half / 5);
      lcd.setDrawColor(y >= start && y <= end ? 0 : 1);
      lcd.drawHLine(1, y, width / 2 - 1);
    }
    lcd.setDrawColor(1);
    lcd.drawTriangle(width - 8, middle, width, middle - 9, width, middle + 9);
    lcd.setDrawColor(0);
    lcd.drawLine(width - 9, middle, width - 2, middle - 7);
    lcd.drawLine(width - 9, middle, width - 2, middle + 7);
    if (value.climb) {
      lcd.setFont(leaf_5x8);
      lcd.drawGlyph(width - 6, middle + 5, rate < 0 ? '-' : '+');
    }
    lcd.setDrawColor(1);
  }
  template <typename Display>
  void drawInstructorInstruments(Display& lcd, const Receiver& receiver, uint32_t now,
                                 const InstructorUnits& units, bool radioReady, bool phone,
                                 const char* notice) {
    const auto value = instructorFlight(receiver, now, units);
    char text[24];
    lcd.setFont(leaf_icons);
    lcd.drawGlyph(0, 12, !radioReady ? '!' : value.position ? '+' : ')');
    lcd.setFont(leaf_7x10);
    if (value.position)
      snprintf(text, sizeof(text), "%03u*", value.heading);
    else
      snprintf(text, sizeof(text), "---");
    lcd.drawStr(25, 11, text);
    if (value.position)
      snprintf(text, sizeof(text), "%u", value.speedUnits);
    else
      snprintf(text, sizeof(text), "--");
    instructorNumber(lcd, text, 60, 14, 35, leaf_8x14);
    lcd.setFont(leaf_5x8);
    instructorChoice(lcd, 24, units.speedMph ? "mph" : "km/h");
    if (value.altitude) {
      const int32_t magnitude =
          value.altitudeUnits < 0 ? -value.altitudeUnits : value.altitudeUnits;
      if (magnitude >= 1000)
        snprintf(text, sizeof(text), "%s%ld,%03ld", value.altitudeUnits < 0 ? "-" : "",
                 (long)(magnitude / 1000), (long)(magnitude % 1000));
      else
        snprintf(text, sizeof(text), "%ld", (long)value.altitudeUnits);
    } else
      snprintf(text, sizeof(text), "---");
    instructorNumber(lcd, text, 15, 32, 47, leaf_10x17);
    lcd.setFont(leaf_6x12);
    lcd.drawGlyph(63, 32, units.altitudeFeet ? 128 : 127);
    lcd.setFont(leaf_5x8);
    instructorChoice(lcd, 38, value.barometric ? "BARO" : "GPS");
    drawInstructorVario(lcd, value);
    constexpr uint8_t x = thermal_page::CORE_VARIO_BAR_WIDTH;
    constexpr uint8_t top = thermal_page::CORE_CLIMB_BOX_TOP;
    constexpr uint8_t width = thermal_page::CORE_CLIMB_BOX_WIDTH;
    lcd.drawBox(x, top, width, thermal_page::CORE_CLIMB_BOX_HEIGHT + 1);
    lcd.setDrawColor(0);
    if (!value.climb)
      snprintf(text, sizeof(text), "---");
    else {
      const int32_t magnitude = value.climbCms < 0 ? -int32_t(value.climbCms) : value.climbCms;
      if (units.climbFpm)
        snprintf(text, sizeof(text), "%ld", (long)((magnitude * 197 / 1000) * 10));
      else
        snprintf(text, sizeof(text), "%ld.%ld", (long)((magnitude + 5) / 100),
                 (long)(((magnitude + 5) / 10) % 10));
    }
    instructorNumber(lcd, text, x + 1, top + 15, width - 10, leaf_8x14);
    lcd.setFont(leaf_6x12);
    lcd.drawGlyph(x + width - 7, top + 14, units.climbFpm ? 132 : 131);
    lcd.setDrawColor(1);
    if (notice) {
      instructorNotice(lcd, notice, 16, 62, 80);
    } else {
      lcd.setFont(leaf_6x12);
      const char* name = receiver.students[receiver.focus].pilotName;
      if (name[0])
        instructorPilotLabel(lcd, name, 16, 72, phone ? 66 : 80);
      else {
        snprintf(text, sizeof(text), "Student %u", receiver.focus + 1);
        lcd.drawStr(16, 72, text);
      }
      if (phone) {
        lcd.setFont(leaf_icons);
        lcd.drawGlyph(84, 72, '%');
      }
    }
    lcd.drawHLine(0, 77, 96);
  }
}  // namespace training
