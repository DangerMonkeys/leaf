#include <Arduino.h>
#include <Preferences.h>
#include <assert.h>
#include <fstream>
#include "comms/fanet_radio.h"
#include "display_capture.h"
#include "runtime.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "training/student.h"
#include "ui/display/display.h"
#include "ui/display/pages.h"
#include "ui/display/pages/menu/page_menu_training.h"
#include "ui/settings/settings.h"
static void screen(const char* path) {
  auto png = sim::encodePng(sim::captureFrame(), 4);
  std::ofstream out(path, std::ios::binary);
  out.write(png.data(), png.size());
  assert(out.good());
}
int main() {
  auto& device = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.statePath = "sim/build-tests/training-student/state";
  options.cardRoot = "sim/build-tests/training-student/card";
  device.configure(options);
  device.boot();
  sim::board().fanetPresent = true;
  fanetRadio.setup();
  settings.fanet_region = FanetRadioRegion::US;
  auto& student = training::student;
  student.setEnabled(false);
  student.unpair();
  fanetRadio.begin(settings.fanet_region);
  // Exercise actual Leaf Labs navigation and actual Training menu buttons.
  leafLabsMenuPage.resetCursor();
  leafLabsMenuPage.button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
  leafLabsMenuPage.button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
  leafLabsMenuPage.draw();
  screen("sim/build-tests/training-student/labs.png");
  leafLabsMenuPage.button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
  auto* menu = mainMenuPage.get_modal_page();
  assert(menu);
  menu->draw();
  screen("sim/build-tests/training-student/disabled.png");
  assert(!student.enabled());
  menu->button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
  assert(student.enabled());
  student.update();
  assert(fanetRadio.trainingReserved());
  menu->button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
  menu->button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
  assert(student.pairing());
  menu->draw();
  screen("sim/build-tests/training-student/searching.png");
  menu->button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
  assert(!student.pairing());
  menu->button_event(Button::UP, ButtonEvent::CLICKED, 0);
  menu->button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
  student.update();
  assert(!student.enabled() && !fanetRadio.trainingReserved());
  assert(fanetRadio.getState() == FanetRadioState::RUNNING);
  for (unsigned i = 0; i < 8; ++i) {
    assert(student.setEnabled(true));
    student.update();
    assert(fanetRadio.trainingReserved());
    assert(student.setEnabled(false));
    student.update();
    assert(!fanetRadio.trainingReserved());
  }
  assert(student.setEnabled(true));
  student.update();
  fanetRadio.end();  // flight stopped while training owns radio
  assert(student.setEnabled(false));
  student.update();
  assert(fanetRadio.getState() == FanetRadioState::UNINITIALIZED);
  // Migration preserves this bench student's enrollment and enables its prior working link.
  Preferences legacy;
  legacy.begin("training-v1");
  legacy.clear();
  legacy.putUInt("group", 7101);
  legacy.putUInt("instructor", 42);
  legacy.putUChar("slot", 2);
  legacy.end();
  training::Student migrated;
  migrated.begin();
  assert(migrated.enabled() && migrated.paired() && migrated.instructor() == 42 &&
         migrated.slot() == 2);
  migrated.setEnabled(false);
  assert(migrated.paired());
  migrated.setEnabled(true);
  assert(migrated.instructor() == 42 && migrated.slot() == 2);
  migrated.unpair();
  assert(!migrated.paired());
  puts(
      "Student UI, radio lifecycle stand-in, repeated toggles and legacy pairing migration passed; "
      "no RF simulated");
}
