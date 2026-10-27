#include <Arduino.h>
#include <Preferences.h>
#include <assert.h>
#include <filesystem>
#include <fstream>
#include "comms/fanet_radio.h"
#include "display_capture.h"
#include "profiles/profile_store.h"
#include "runtime.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "sim/training_radio.h"
#include "training/pilot_name.h"
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
  const auto profiles = std::filesystem::path(options.cardRoot) / "profiles";
  std::filesystem::create_directories(profiles);
  auto writeProfile = [&](const char* json) {
    std::ofstream f(profiles / "profiles.json");
    f << json;
    assert(f.good());
  };
  writeProfile(
      R"({"schema":"leaf.profiles","active_pilot_id":"a","pilots":[{"id":"a","name":"Alex Taylor"},{"id":"b","name":"Beth Wong"}]})");
  PilotProfile pilot;
  assert(ProfileStore::activePilot(pilot) && pilot.name == "Alex Taylor");
  assert(ProfileStore::selectPilot("b"));
  assert(ProfileStore::activePilot(pilot) && pilot.name == "Beth Wong");
  writeProfile(R"({"schema":"leaf.profiles","pilots":[{"id":"b","name":"Beth Renamed"}]})");
  assert(ProfileStore::activePilot(pilot) && pilot.name == "Beth Renamed");
  writeProfile(
      R"({"schema":"leaf.profiles","pilots":[{"id":"a","name":"Alex"},{"id":"b","name":"Beth"}]})");
  assert(!ProfileStore::activePilot(pilot));
  std::filesystem::remove(profiles / "profiles.json");
  assert(!ProfileStore::activePilot(pilot));

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
  // Exercise the real student adapter, not just the pure link state, with no instructor polls.
  legacy.begin("training-v1");
  const uint32_t binding[] = {7101, 42, 2};
  legacy.putBytes("binding", binding, sizeof(binding));
  legacy.putBool("enabled", true);
  legacy.end();
  student.begin();
  sim::training_radio::beginCapture();
  auto run = [&](uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 10) {
      sim::clock().advanceUs(10000);
      student.update();
    }
  };
  run(2200);
  auto verifyFrames = [&] {
    uint32_t previous = 0;
    for (const auto& frame : sim::training_radio::sent()) {
      const auto& p = frame.packet;
      assert(p.header.type == training::Type::Broadcast && p.header.group == 7101 &&
             p.header.sender == training::deviceId() && p.data.slot == 2);
      assert(!previous || training::newer(p.header.sequence, previous));
      previous = p.header.sequence;
      uint8_t bytes[training::MAX_PACKET];
      assert(training::encode(p, bytes, sizeof(bytes)) == 64);
    }
  };
  verifyFrames();
  assert(sim::training_radio::sent().size() >= 7 && sim::training_radio::sent().size() <= 10);
  sim::training_radio::beginCapture();
  training::Packet focus;
  focus.header = {training::Type::Focus, 7101, 42, 11, 1};
  focus.focusSlot = 0;
  sim::training_radio::inject(focus);
  run(1000);
  ++focus.header.sequence;
  sim::training_radio::inject(focus);
  run(1000);
  verifyFrames();
  assert(sim::training_radio::sent().size() >= 1 && sim::training_radio::sent().size() <= 3);
  // Once control is lost, keep transmitting and return to 4 Hz, including after instructor off.
  sim::training_radio::beginCapture();
  run(7000);
  verifyFrames();
  unsigned fastFrames = 0;
  for (const auto& frame : sim::training_radio::sent())
    if (uint32_t(millis() - frame.at) < 4000) ++fastFrames;
  assert(fastFrames >= 14 && fastFrames <= 18);
  assert(student.setEnabled(false));
  sim::training_radio::beginCapture();
  run(3000);
  assert(sim::training_radio::sent().empty() && !fanetRadio.trainingReserved());
  student.setEnabled(true);
  student.unpair();
  sim::training_radio::beginCapture();
  run(1000);
  assert(sim::training_radio::sent().empty());
  sim::training_radio::endCapture();
  puts("Actual student adapter: autonomous 4 Hz, optional 1 Hz focus control, fallback after control loss, enable/unpair gating passed");
  puts(
      "Student UI, radio lifecycle stand-in, repeated toggles and legacy pairing migration passed; "
      "no RF simulated");
}
