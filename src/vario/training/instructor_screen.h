#pragma once
#include <stdio.h>
#include "instructor_battery.h"
#include "instructor_instruments.h"
namespace training {
  template <typename Display>
  void drawInstructorRoster(Display& lcd, const Receiver& receiver, uint32_t now,
                            const InstructorNavigation& navigation, const InstructorUnits& units,
                            bool radioReady, bool phone, uint8_t volume, const char* notice,
                            const InstructorBattery* battery = nullptr) {
    drawInstructorInstruments(lcd, receiver, now, units, radioReady, phone, notice);
    char text[24];
    for (uint8_t i = 0; i < STUDENTS; ++i) {
      const auto& s = receiver.students[i];
      const uint8_t y = 97 + i * 22;
      instructorRow(lcd, y, navigation.row == i);
      if (s.pilotName[0]) {
        snprintf(text, sizeof(text), "%u", i + 1);
        lcd.drawStr(3, y, text);
        instructorPilotLabel(lcd, s.pilotName, 15, y, 58);
      } else {
        if (s.id)
          snprintf(text, sizeof(text), "%u %08lx", i + 1, (unsigned long)s.id);
        else
          snprintf(text, sizeof(text), "%u Unpaired", i + 1);
        lcd.drawStr(3, y, text);
      }
      if (s.id) {
        if (receiver.focus == i) {
          lcd.setFont(leaf_icons);
          lcd.drawGlyph(74, y, 'I' + volume);
        }
        const bool live = s.received && uint32_t(now - s.receivedAt) <
                                            (i == receiver.focus ? LINK_STALE_MS : 2500);
        if (live)
          lcd.drawDisc(90, y - 6, 3);
        else {
          lcd.drawCircle(90, y - 6, 3);
          lcd.drawLine(87, y - 9, 93, y - 3);
        }
      }
      lcd.setDrawColor(1);
    }
    instructorRow(lcd, 189, navigation.row == InstructorNavigation::SETTINGS_ROW);
    lcd.drawGlyph(3, 189, 151);
    lcd.drawStr(20, 189, "Settings");
    if (battery && battery->valid) {
      lcd.setFont(leaf_icons);
      const char glyph = battery->icon();
      const char label[] = {glyph, 0};
      lcd.drawGlyph(94 - lcd.getStrWidth(label), 189, glyph);
    }
    lcd.setDrawColor(1);
  }
  template <typename Display>
  void drawInstructorSettings(Display& lcd, const InstructorNavigation& navigation, uint8_t focus,
                              uint32_t student, uint32_t id, const InstructorUnits& units,
                              const char* volume, const char* phone, bool connected,
                              const char* notice = nullptr, const char* pilotName = nullptr) {
    char text[24];
    if (navigation.page == InstructorPage::Remove) {
      instructorTitle(lcd, "Unpair?");
      if (pilotName && pilotName[0]) {
        char full[PILOT_NAME_BYTES + 1];
        pilotNameForDisplay(full, pilotName);
        instructorNotice(lcd, full, 3, 35, 90, 4);
        lcd.setFont(leaf_6x12);
        snprintf(text, sizeof(text), "%u %08lx", focus + 1, (unsigned long)student);
        lcd.drawStr(3, 89, text);
      } else {
        snprintf(text, sizeof(text), "Student %u", focus + 1);
        lcd.drawStr(3, 44, text);
        snprintf(text, sizeof(text), "%08lx", (unsigned long)student);
        lcd.drawStr(3, 64, text);
      }
      instructorRow(lcd, 119, navigation.confirmation == 0);
      lcd.drawStr(3, 119, "Cancel");
      instructorRow(lcd, 145, navigation.confirmation == 1);
      lcd.drawStr(3, 145, "Unpair");
      lcd.setDrawColor(1);
      return;
    }
    instructorTitle(lcd, "Settings");
    const char* labels[] = {"Volume",      "Altitude", "Speed", "Climb",
                            "Add student", "Unpair",   "Back"};
    for (uint8_t i = 0; i < InstructorNavigation::SETTINGS_ITEMS; ++i) {
      const uint8_t y = 39 + i * 20;
      instructorRow(lcd, y, navigation.setting == i);
      lcd.drawStr(3, y, labels[i]);
      if (i == 0) instructorChoice(lcd, y, volume);
      if (i == 1) instructorChoice(lcd, y, units.altitudeFeet ? "ft" : "m");
      if (i == 2) instructorChoice(lcd, y, units.speedMph ? "mph" : "km/h");
      if (i == 3) instructorChoice(lcd, y, units.climbFpm ? "fpm" : "m/s");
      if (i == 5) {
        snprintf(text, sizeof(text), "%u", focus + 1);
        instructorChoice(lcd, y, text);
      }
      lcd.setDrawColor(1);
    }
    if (notice)
      instructorNotice(lcd, notice, 3, 178, 90);
    else {
      lcd.setFont(leaf_5x8);
      lcd.drawStr(3, 178, phone);
      if (connected) {
        lcd.setFont(leaf_icons);
        lcd.drawGlyph(82, 178, '%');
      }
      lcd.setFont(leaf_5x8);
      snprintf(text, sizeof(text), "ID %08lx", (unsigned long)id);
      lcd.drawStr(3, 191, text);
    }
  }
  template <typename Display>
  void drawInstructorPower(Display& lcd, bool powered, uint32_t holding) {
    lcd.setFont(leaf_6x12);
    lcd.setDrawColor(1);
    if (holding >= InstructorButtons::PREVIEW_MS) {
      lcd.drawStr(3, 82, powered ? "Power off" : "Power on");
      lcd.drawFrame(0, 98, 96, 12);
      const uint32_t width =
          holding >= InstructorButtons::HOLD_MS ? 92 : holding * 92 / InstructorButtons::HOLD_MS;
      lcd.drawBox(2, 100, width, 8);
    } else
      lcd.drawStr(3, 96, "Power off");
  }
}  // namespace training
