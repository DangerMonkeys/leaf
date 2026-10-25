#pragma once
#include <stdio.h>
#include "instructor_link.h"
#include "instructor_ui.h"
namespace training {
  template <typename Display>
  void drawInstructorPairing(Display& lcd, const InstructorLink& link,
                             const InstructorRoster& roster, uint32_t now, uint32_t id) {
    char text[24];
    instructorTitle(lcd, "Add student");
    lcd.setFont(leaf_5x8);
    snprintf(text, sizeof(text), "ID %08lx", (unsigned long)id);
    lcd.drawStr(3, 33, text);
    snprintf(text, sizeof(text), "%lus", (unsigned long)link.remaining(now));
    instructorChoice(lcd, 49, text);
    if (link.phase(now) == InstructorLink::Phase::Discovering) {
      bool any = false;
      for (uint8_t i = 0; i < STUDENTS; ++i) {
        const auto& c = link.candidates[i];
        if (!c.fresh(now)) continue;
        any = true;
        const uint8_t y = 78 + i * 22;
        instructorRow(lcd, y, i == link.selection, 21);
        snprintf(text, sizeof(text), "%08lx", (unsigned long)c.id);
        if (c.pilotName[0]) {
          instructorPilotLabel(lcd, c.pilotName, 3, y - 8, 74);
          lcd.setFont(leaf_5x8);
          lcd.drawStr(3, y + 1, text);
        } else
          lcd.drawStr(3, y - 3, text);
        if (roster.slotFor(c.id) >= 0) {
          lcd.setFont(leaf_icons);
          lcd.drawGlyph(82, y - 3, 'Q');
        }
        lcd.setDrawColor(1);
      }
      if (!any) {
        lcd.setFont(leaf_6x12);
        lcd.drawStr(3, 72, "Searching...");
      }
      lcd.setFont(leaf_5x8);
      lcd.drawStr(3, 160, "Labs > Training");
      lcd.drawStr(3, 172, "Enable / Pair");
    } else {
      lcd.setFont(leaf_6x12);
      snprintf(text, sizeof(text), "%08lx", (unsigned long)link.peer());
      if (link.peerName()[0]) {
        instructorPilotLabel(lcd, link.peerName(), 3, 68, 90);
        lcd.setFont(leaf_5x8);
        lcd.drawStr(3, 80, text);
        lcd.setFont(leaf_6x12);
      } else
        lcd.drawStr(3, 70, text);
      snprintf(text, sizeof(text), "Student %u", link.slot() + 1);
      lcd.drawStr(3, 92, text);
      if (link.phase(now) == InstructorLink::Phase::Offering) {
        lcd.drawStr(3, 122, "On student:");
        lcd.drawStr(3, 140, "Check ID,");
        lcd.drawStr(3, 158, "Confirm pair");
      } else {
        lcd.drawStr(3, 122, "Link saved");
        lcd.drawStr(3, 144, "Awaiting data");
      }
    }
    lcd.setFont(leaf_6x12);
    lcd.drawGlyph(3, 190, 124);
    lcd.drawStr(20, 190, "Back");
  }
}  // namespace training
