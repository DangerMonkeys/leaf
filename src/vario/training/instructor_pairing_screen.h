#pragma once
#include <stdio.h>
#include "instructor_link.h"
namespace training {
  // Caller owns the framebuffer/font; same rendering is exercised by the host screenshot test.
  template <typename Display>
  void drawInstructorPairing(Display& lcd, const InstructorLink& link,
                             const InstructorRoster& roster, uint32_t now, uint32_t id) {
    char line[24];
    lcd.drawStr(0, 8, "ADD STUDENT");
    snprintf(line, sizeof(line), "Inst %08lx", (unsigned long)id);
    lcd.drawStr(0, 20, line);
    snprintf(line, sizeof(line), "Pair window: %lus", (unsigned long)link.remaining(now));
    lcd.drawStr(0, 32, line);
    if (link.phase(now) == InstructorLink::Phase::Discovering) {
      bool any = false;
      for (uint8_t i = 0; i < STUDENTS; ++i) {
        const auto& c = link.candidates[i];
        if (!c.fresh(now)) continue;
        any = true;
        snprintf(line, sizeof(line), "%c %08lx %s", i == link.selection ? '>' : ' ',
                 (unsigned long)c.id, roster.slotFor(c.id) >= 0 ? "saved" : "new");
        lcd.drawStr(0, 48 + i * 14, line);
      }
      if (!any) lcd.drawStr(0, 62, "Searching...");
      lcd.drawStr(0, 110, "On student Leaf:");
      lcd.drawStr(0, 122, "Training On > Pair");
      lcd.drawStr(0, 152, "Up/down: select");
      lcd.drawStr(0, 164, "Right: link student");
    } else {
      snprintf(line, sizeof(line), "Student %08lx", (unsigned long)link.peer());
      lcd.drawStr(0, 48, line);
      snprintf(line, sizeof(line), "Assigned slot %u", link.slot() + 1);
      lcd.drawStr(0, 62, line);
      if (link.phase(now) == InstructorLink::Phase::Offering) {
        lcd.drawStr(0, 90, "Check Inst ID on");
        lcd.drawStr(0, 102, "the student, then");
        lcd.drawStr(0, 114, "select Confirm pair");
      } else {
        lcd.drawStr(0, 90, "Pairing saved");
        lcd.drawStr(0, 102, "Waiting for data...");
        lcd.drawStr(0, 126, "No GPS fix needed");
      }
    }
    lcd.drawStr(0, 176, "Left: close pairing");
    lcd.drawStr(0, 188, "Saved links kept");
  }
}  // namespace training
