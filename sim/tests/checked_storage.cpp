#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <assert.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "diagnostics/heap_monitor.h"
#include "display_capture.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "navigation/user_waypoints.h"
#include "profiles/profile_store.h"
#include "runtime.h"
#include "sim/clock.h"
#include "storage/checked_file.h"
#include "storage/sd_card.h"
#include "ui/display/pages.h"
#include "ui/display/pages/dialogs/page_storage_warning.h"
#include "ui/display/pages/menu/page_gpx_file_select.h"
#include "ui/display/pages/primary/page_menu_main.h"
#include "ui/settings/settings.h"

namespace {
  std::string root, failStage;
  unsigned opens = 0;
  int fault(const char* stage) {
    if (!strcmp(stage, "open")) ++opens;
    return failStage == stage;
  }
  void faults(const char* stage = "") {
    failStage = stage;
    opens = 0;
    checked_file::setFaultHook(fault);
  }
  void raw(const std::string& name, const std::string& text) {
    std::ofstream file(root + name);
    file << text;
    assert(file.good());
  }
  std::string read(const std::string& name) {
    std::ifstream file(root + name);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
  }
  void replacementFailures() {
    JsonDocument doc;
    doc["value"] = "replacement";
    for (const char* stage :
         {"open", "write", "short_write", "flush", "close", "verify", "backup", "promote"}) {
      raw("/profiles/checked-test.json", "{\"value\":\"original\"}");
      faults(stage);
      assert(!checked_file::replaceJson("/profiles/checked-test.json", doc));
      assert(read("/profiles/checked-test.json") == "{\"value\":\"original\"}");
      assert(!std::filesystem::exists(root + "/profiles/checked-test.json.tmp"));
      assert(!checked_file::replaceBytes("/profiles/checked-test.json", "replacement bytes"));
      assert(read("/profiles/checked-test.json") == "{\"value\":\"original\"}");
    }
    faults();
    assert(checked_file::replaceJson("/profiles/checked-test.json", doc));
    assert(read("/profiles/checked-test.json") == "{\"value\":\"replacement\"}");
    assert(checked_file::replaceBytes("/profiles/checked-test.json", "verified bytes"));
    assert(read("/profiles/checked-test.json") == "verified bytes");
    // Simulate an interrupted promotion: old main is the backup, staged bytes are untrusted.
    std::filesystem::rename(root + "/profiles/checked-test.json",
                            root + "/profiles/checked-test.json.bak");
    raw("/profiles/checked-test.json.tmp", "incomplete");
    assert(checked_file::recover("/profiles/checked-test.json"));
    assert(read("/profiles/checked-test.json") == "verified bytes");
    // A complete main wins over an older backup.
    raw("/profiles/checked-test.json.bak", "older bytes");
    assert(checked_file::recover("/profiles/checked-test.json"));
    assert(read("/profiles/checked-test.json") == "verified bytes");
    assert(!checked_file::replaceBytes("/../escape", "bad path"));
    assert(checked_file::erase("/profiles/checked-test.json"));
    assert(checked_file::recover("/profiles/checked-test.json"));
    assert(!std::filesystem::exists(root + "/profiles/checked-test.json"));
    assert(!std::filesystem::exists(root + "/profiles/checked-test.json.bak"));
  }
  void actualCallers() {
    raw("/profiles/profiles.json",
        R"({"schema":"leaf.profiles","pilots":[{"id":"p1","name":"First"},{"id":"p2","name":"Second"}],"gliders":[],"active_pilot_id":"p1"})");
    const auto profile = read("/profiles/profiles.json");
    faults("close");
    assert(!ProfileStore::selectPilot("p2"));
    assert(read("/profiles/profiles.json") == profile);
    faults();
    assert(ProfileStore::selectPilot("p2"));
    PilotProfile pilot;
    assert(ProfileStore::activePilot(pilot) && pilot.id == "p2");

    raw("/waypoints/user_waypoints.json",
        R"({"schema":"leaf.user_waypoints","points":[{"id":"a","name":"Original","lat":47.5,"lon":-122.2}]})");
    const auto waypoint = read("/waypoints/user_waypoints.json");
    faults("flush");
    String error;
    assert(!user_waypoints::deleteById("a", error));
    assert(read("/waypoints/user_waypoints.json") == waypoint);
    faults();

    raw("/waypoints/bench.cup",
        "name,code,country,lat,lon,elev,style,rwdir,rwlen,freq,desc\nBench,B1,US,4730.000N,12212."
        "000W,100m,1,,,,\n");
    assert(nav_readFile(SD_MMC, "/waypoints/bench.cup"));
    const auto active = read("/routes/active.json");
    faults("open");
    navigator.clear();
    assert(navigator.loadPersistedState());
    assert(opens == 0 && navigator.totalWaypoints == 1);
    assert(read("/routes/active.json") == active);
    assert(!navigator.savePersistedState());
    assert(read("/routes/active.json") == active);
    faults();
    raw("/routes/recovered.json.bak",
        R"({"schema":"leaf.route","name":"Recovered","points":[{"name":"A","lat":47.5,"lon":-122.2,"radius_m":400}]})");
    PageGpxFileSelect picker;
    picker.show();
    picker.button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
    picker.button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
    picker.button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
    assert(navigator.totalRoutes == 1 && String(navigator.routes[1].name) == "Recovered");
    assert(std::filesystem::exists(root + "/routes/recovered.json"));
    mainMenuPage.pop_all_pages();
  }
  void stackSave() {
    settings.dev_mode = true;
    for (const char* stage : {"write", "flush", "close", "verify"}) {
      heap_monitor::startStackMonitor();
      assert(heap_monitor::stackMonitorState() == heap_monitor::StackMonitorState::Running);
      sim::clock().advanceUs(1000000);
      faults(stage);
      assert(!heap_monitor::saveStackMonitor());
      assert(heap_monitor::stackMonitorState() == heap_monitor::StackMonitorState::Frozen);
      faults();
      assert(heap_monitor::saveStackMonitor());
      assert(heap_monitor::stackMonitorState() == heap_monitor::StackMonitorState::Idle);
    }
    for (const auto& file : std::filesystem::directory_iterator(root + "/diagnostics"))
      if (file.path().extension() == ".csv") assert(file.file_size() > 40);
  }
  void screen(const std::string& output, const char* name) {
    auto* page = mainMenuPage.get_modal_page();
    assert(page);
    page->draw();
    const auto png = sim::encodePng(sim::captureFrame(), 1);
    std::ofstream file(output + "/" + name + ".png", std::ios::binary);
    file.write(png.data(), png.size());
    assert(file.good());
    page->button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
    assert(!mainMenuPage.get_modal_page());
  }
  void warnings(const std::string& output) {
    std::filesystem::create_directories(output);
    mainMenuPage.pop_all_pages();
    SD_MMC.simSetSpace(8000000000ULL, 10000001);
    storage_warning::checkAtStartup();
    assert(!mainMenuPage.get_modal_page());
    SD_MMC.simSetSpace(8000000000ULL, 10000000);
    storage_warning::checkAtStartup();
    screen(output, "threshold-10mb");
    SD_MMC.simSetSpace(8000000000ULL, 8000000);
    storage_warning::checkAtStartup();
    screen(output, "low-space");
    SD_MMC.simSetSpace(8000000000ULL, 16384);
    storage_warning::checkAtStartup();
    screen(output, "less-than-01mb");
    SD_MMC.simSetSpace(8000000000ULL, 0);
    storage_warning::checkAtStartup();
    screen(output, "full-card");
    SD_MMC.simSetPresent(false);
    storage_warning::checkAtStartup();
    screen(output, "unavailable-card");
    SD_MMC.simSetPresent(true);
    assert(sdcard.mount());
    SD_MMC.simSetSpace(0, 0);
    storage_warning::checkAtStartup();
    screen(output, "unknown-capacity");
    SD_MMC.simSetSpace(8000000000ULL, 0);
    settings.log_saveTrack = false;
    flightTimer_start();
    storage_warning::checkAtStartup();
    assert(!mainMenuPage.get_modal_page());
    flightTimer_stop(false);
  }
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  char temporary[] = "/tmp/leaf-checked-storage-XXXXXX";
  root = mkdtemp(temporary);
  for (const char* folder : {"routes", "profiles", "waypoints", "logbook", "tracks", "diagnostics"})
    std::filesystem::create_directories(root + "/" + folder);
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.cardRoot = root;
  options.statePath = root + "/state";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.system_autoOff = settings.log_autoStart = settings.log_autoStop = false;
  replacementFailures();
  actualCallers();
  stackSave();
  warnings(argv[1]);
  checked_file::setFaultHook(nullptr);
  std::filesystem::remove_all(root);
  puts(
      "Checked storage: replacement faults, rollback, recovery, caller preservation, read-only "
      "restore, Stack retry and startup warning boundaries passed.");
}
