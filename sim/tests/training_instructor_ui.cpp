#include <Arduino.h>
#include <assert.h>
#include <string.h>
#include <fstream>
#include "display_capture.h"
#include "training/instructor_pairing_screen.h"
#include "training/instructor_screen.h"
#include "ui/display/display.h"
using namespace training;
// Check actual glyph widths and vertical extents, including full-width hexadecimal IDs.
struct ScreenBounds {
  void setFont(const uint8_t* font) {
    assert(font == leaf_6x12 || font == leaf_5x8 || font == leaf_8x14 || font == leaf_10x17 ||
           font == leaf_7x10 || font == leaf_icons);
    u8g2.setFont(font);
  }
  unsigned getStrWidth(const char* text) { return u8g2.getStrWidth(text); }
  void setDrawColor(uint8_t color) {
    assert(color <= 2);
    u8g2.setDrawColor(color);
  }
  void textBounds(int x, int y, unsigned width) {
    assert(x >= 0 && y >= u8g2.getAscent() && x + width <= 96 && y - u8g2.getDescent() < 192);
  }
  void drawStr(int x, int y, const char* text) {
    const auto width = getStrWidth(text);
    if (x < 0 || x + width > 96) fprintf(stderr, "Text too wide: %s (%u at %d)\n", text, width, x);
    textBounds(x, y, width);
    u8g2.drawStr(x, y, text);
  }
  void drawGlyph(int x, int y, uint16_t glyph) {
    textBounds(x, y, u8g2_GetGlyphWidth(u8g2.getU8g2(), glyph));
    u8g2.drawGlyph(x, y, glyph);
  }
  void shape(int x, int y, int w, int h) {
    assert(x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= 96 && y + h <= 192);
  }
  void drawHLine(int x, int y, int w) {
    shape(x, y, w, 1);
    u8g2.drawHLine(x, y, w);
  }
  void drawFrame(int x, int y, int w, int h) {
    shape(x, y, w, h);
    u8g2.drawFrame(x, y, w, h);
  }
  void drawBox(int x, int y, int w, int h) {
    shape(x, y, w, h);
    u8g2.drawBox(x, y, w, h);
  }
  void drawRBox(int x, int y, int w, int h, int r) {
    shape(x, y, w, h);
    u8g2.drawRBox(x, y, w, h, r);
  }
  void drawLine(int x, int y, int x2, int y2) {
    shape(x, y, 1, 1);
    shape(x2, y2, 1, 1);
    u8g2.drawLine(x, y, x2, y2);
  }
  void drawTriangle(int x, int y, int x2, int y2, int x3, int y3) {
    shape(x, y, 1, 1);
    shape(x2, y2, 1, 1);
    shape(x3, y3, 1, 1);
    u8g2.drawTriangle(x, y, x2, y2, x3, y3);
  }
  void drawDisc(int x, int y, int r) {
    shape(x - r, y - r, 2 * r + 1, 2 * r + 1);
    u8g2.drawDisc(x, y, r);
  }
  void drawCircle(int x, int y, int r) {
    shape(x - r, y - r, 2 * r + 1, 2 * r + 1);
    u8g2.drawCircle(x, y, r);
  }
} bounds;
template <typename Draw>
static void screen(const char* path, Draw draw) {
  u8g2.clearBuffer();
  draw();
  auto png = sim::encodePng(sim::captureFrame(), 4);
  std::ofstream file(path, std::ios::binary);
  file.write(png.data(), png.size());
  assert(file.good());
}
int main() {
  u8g2.begin();
  InstructorLink link;
  InstructorRoster roster;
  roster.group = 7101;
  link.open(100);
  auto pairing = [&](uint32_t now) {
    drawInstructorPairing(bounds, link, roster, now, 0x594362e6);
  };
  screen("sim/build-tests/instructor-search.png", [&] { pairing(100); });
  Packet hello;
  hello.header = {Type::Hello, DISCOVERY_GROUP, 0x56bd55e6, 77, 99};
  assert(link.hello(hello, 200));
  for (unsigned i = 1; i < STUDENTS; ++i) {
    hello.header.sender = 0xffffffff - i;
    assert(link.hello(hello, 201));
  }
  roster.ids[1] = hello.header.sender;  // Exercise longer 'saved' label, with four candidates.
  screen("sim/build-tests/instructor-found.png", [&] { pairing(201); });
  copyPilotName(link.candidates[0].pilotName, "Alex Taylor");
  copyPilotName(link.candidates[1].pilotName, "Christopher Robertson");
  copyPilotName(link.candidates[2].pilotName, "Beth Wong");
  screen("sim/build-tests/instructor-found-names.png", [&] { pairing(201); });
  assert(link.choose(roster, 202));
  screen("sim/build-tests/instructor-offer.png", [&] { pairing(202); });
  link.committed(203);
  screen("sim/build-tests/instructor-confirm.png", [&] { pairing(203); });
  Receiver receiver;
  InstructorNavigation nav;
  InstructorUnits units;
  receiver.focus = 0;
  receiver.students[0].id = 0x56bd55e6;
  for (unsigned i = 0; i < STUDENTS; ++i) {
    auto& s = receiver.students[i];
    if (i) s.id = 0xffffffff - i;
    s.received = true;
    s.receivedAt = i == 2 ? 0 : 2990;
    s.data.valid = Climb | Position | Baro;
    s.data.climbCms = 234;
    s.data.pressureAge = s.data.gpsAge = 0;
    s.data.gpsAltitudeCm = 123400;
    s.data.baroAltitudeCm = 128000;
    s.data.speedCms = 1000;
    s.data.courseCdeg = 27199;
  }
  auto v = instructorFlight(receiver, 3000, units);
  assert(v.position && v.climb && v.altitude && !v.barometric);
  assert(v.altitudeUnits == 1234 && v.speedUnits == 36 && v.heading == 271);
  auto rosterScreen = [&](const char* path, uint32_t now, bool ready = true,
                          const char* notice = nullptr) {
    screen(path, [&] {
      drawInstructorRoster(bounds, receiver, now, nav, units, ready, true, 3, notice);
    });
  };
  rosterScreen("sim/build-tests/instructor-roster.png", 3000);
  InstructorBattery battery;
  for (bool selected : {false, true}) {
    nav.row = selected ? 4 : 0;
    for (bool charging : {false, true})
      for (uint8_t percent : {uint8_t(0), uint8_t(10), uint8_t(50), uint8_t(90), uint8_t(100)}) {
        battery.valid = true; battery.charging = charging; battery.percent = percent;
        drawInstructorRoster(bounds, receiver, 3000, nav, units, true, true, 3, nullptr, &battery);
      }
  }
  battery.valid = true; battery.charging = true; battery.percent = 50;
  screen("sim/build-tests/instructor-battery-charging.png", [&] {
    drawInstructorRoster(bounds, receiver, 3000, nav, units, true, true, 3, nullptr, &battery);
  });
  battery.charging = false;
  screen("sim/build-tests/instructor-battery-half.png", [&] {
    drawInstructorRoster(bounds, receiver, 3000, nav, units, true, true, 3, nullptr, &battery);
  });
  nav.row = 4;
  rosterScreen("sim/build-tests/instructor-settings-cursor.png", 3000);
  units = {true, true, true};
  v = instructorFlight(receiver, 3000, units);
  assert(v.altitudeUnits == 4048 && v.speedUnits == 22);
  rosterScreen("sim/build-tests/instructor-imperial.png", 3000);
  // Age independent GPS and pressure/climb streams, retaining a valid barometer without GPS.
  auto& d = receiver.students[0].data;
  d.gpsAge = 2490;
  v = instructorFlight(receiver, 3000, units);
  assert(!v.position && v.climb && v.altitude && v.barometric);
  rosterScreen("sim/build-tests/instructor-no-gps.png", 3000);
  d.pressureAge = LINK_STALE_MS - 10;
  v = instructorFlight(receiver, 3000, units);
  assert(!v.climb && !v.altitude);
  rosterScreen("sim/build-tests/instructor-stale.png", 3000);
  units = {};
  d.gpsAge = d.pressureAge = 0;
  d.climbCms = -32768;
  d.gpsAltitudeCm = INT32_MIN;
  d.speedCms = UINT16_MAX;
  v = instructorFlight(receiver, 3000, units);
  assert(v.speedUnits == 2359 && v.altitudeUnits == -99999);
  rosterScreen("sim/build-tests/instructor-sink.png", 3000, true, "Student removed");
  // All protocol-representable extremes must remain inside the framebuffer in either units.
  for (bool imperial : {false, true}) {
    units = {imperial, imperial, imperial};
    for (int rate : {-32768, -1001, -1000, -501, -500, -1, 0, 1, 500, 501, 1000, 1001, 32767}) {
      d.climbCms = rate;
      for (int32_t altitude : {INT32_MIN, -12345678, 0, 12345678, INT32_MAX}) {
        d.gpsAltitudeCm = altitude;
        drawInstructorRoster(bounds, receiver, 3000, nav, units, true, false, 0, nullptr);
      }
    }
  }
  receiver.students[3] = {};
  receiver.focus = nav.row = 3;
  rosterScreen("sim/build-tests/instructor-empty.png", 9000, false);
  nav.page = InstructorPage::Settings;
  nav.setting = 0;
  screen("sim/build-tests/instructor-menu.png", [&] {
    drawInstructorSettings(bounds, nav, 0, 0x56bd55e6, 0x594362e6, units, "High", "Inst62E6", true);
  });
  for (uint8_t i = 0; i < InstructorNavigation::SETTINGS_ITEMS; ++i) {
    nav.setting = i;
    drawInstructorSettings(bounds, nav, 0, 0x56bd55e6, 0x594362e6, units, "Med", "Inst62E6", false);
  }
  screen("sim/build-tests/instructor-settings-notice.png", [&] {
    drawInstructorSettings(bounds, nav, 0, 0x56bd55e6, 0x594362e6, units, "High", "Inst62E6", true,
                           "No saved student");
  });
  units = {};
  d.gpsAltitudeCm = 123400; d.baroAltitudeCm = 128000; d.climbCms = 234; d.speedCms = 1000;
  receiver.students[3].id = 0xfffffffc;
  copyPilotName(receiver.students[0].pilotName, "Alex Taylor");
  copyPilotName(receiver.students[1].pilotName, "Beth Wong");
  copyPilotName(receiver.students[2].pilotName, "Jos\xc3\xa9 Garcia");
  copyPilotName(receiver.students[3].pilotName, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW W");
  receiver.focus = nav.row = 0;
  rosterScreen("sim/build-tests/instructor-names.png", 3000);
  for (const char* name : {"Alex Taylor", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW W", "Beth W."})
    for (uint8_t width : {uint8_t(58), uint8_t(66), uint8_t(74), uint8_t(80), uint8_t(90)})
      instructorPilotLabel(bounds, name, 3, 72, width);
  nav.askRemove();
  screen("sim/build-tests/instructor-remove.png", [&] {
    drawInstructorSettings(bounds, nav, 0, 0x56bd55e6, 0x594362e6, units, "High", "Inst62E6", true,
                           nullptr, receiver.students[0].pilotName);
  });
  nav.move(1);
  screen("sim/build-tests/instructor-remove-confirm.png", [&] {
    drawInstructorSettings(bounds, nav, 0, 0x56bd55e6, 0x594362e6, units, "High", "Inst62E6", true);
  });
  screen("sim/build-tests/instructor-off.png", [&] { drawInstructorPower(bounds, false, 0); });
  screen("sim/build-tests/instructor-power-hold.png",
         [&] { drawInstructorPower(bounds, true, 2000); });
  screen("sim/build-tests/instructor-wake.png", [&] { drawInstructorPower(bounds, false, 3000); });
  puts("Instructor screens: Leaf fonts/cursors, units, data ages, extrema and bounds passed");
}
