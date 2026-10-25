#include <Arduino.h>
#include <assert.h>
#include <string.h>
#include <fstream>
#include "display_capture.h"
#include "training/instructor_pairing_screen.h"
#include "ui/display/display.h"
using namespace training;
struct ScreenBounds {
  void drawStr(unsigned x, unsigned y, const char* text) {
    assert(x == 0 && y <= 192 && strlen(text) <= 19);
  }
};
static void screen(const char* path, const InstructorLink& link, const InstructorRoster& roster,
                   uint32_t now) {
  ScreenBounds bounds;
  drawInstructorPairing(bounds, link, roster, now, 0x594362e6);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  drawInstructorPairing(u8g2, link, roster, now, 0x594362e6);
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
  screen("sim/build-tests/instructor-search.png", link, roster, 100);
  Packet hello;
  hello.header = {Type::Hello, DISCOVERY_GROUP, 0x56bd55e6, 77, 99};
  assert(link.hello(hello, 200));
  hello.header.sender = 0xffffffff;
  assert(link.hello(hello, 201));
  screen("sim/build-tests/instructor-found.png", link, roster, 201);
  assert(link.choose(roster, 202));
  screen("sim/build-tests/instructor-offer.png", link, roster, 202);
  link.committed(203);
  screen("sim/build-tests/instructor-confirm.png", link, roster, 203);
  puts("Instructor discovery screens rendered with text bounds checked");
}
