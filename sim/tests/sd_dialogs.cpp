// Visual inventory only: fixtures/faults select states; shipping firmware draws every pixel.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <assert.h>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>

#include "display_capture.h"
#include "instruments/gps.h"
#include "logbook/igc.h"
#include "logbook/logbook_entry.h"
#include "logbook/logbook_io.h"
#include "logbook/logbook_store.h"
#include "logging/log.h"
#include "runtime.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/display/pages/dialogs/page_flight_summary.h"
#include "ui/display/pages/menu/page_logbook.h"
#include "ui/display/pages/menu/page_menu_log.h"
#include "ui/display/pages/primary/page_menu_main.h"
#include "ui/settings/settings.h"

extern LogbookEntryFile logbookEntry;
extern Igc igcFlight;

namespace {
  std::string root, output, faultStage, savingName, shutdownName;
  unsigned failures = 0, opens = 0;
  bool shutdownCaptured = false;
  bool shutdownHasTrack = false;
  JsonDocument manifest;
  PageFlightSummary modalController;

  void snapshot(const std::string& name, const char* title, const char* condition,
                const char* renderer) {
    const auto frame = sim::captureFrame();
    assert(frame.width == 96 && frame.height == 192);
    const auto filePath = std::filesystem::path(output) / "screenshots" / (name + ".png");
    std::filesystem::create_directories(filePath.parent_path());
    const auto png = sim::encodePng(frame, 1);
    std::ofstream file(filePath, std::ios::binary);
    file.write(png.data(), png.size());
    assert(file.good());
    JsonObject item = manifest["screens"].as<JsonArray>().add<JsonObject>();
    item["id"] = name;
    item["title"] = title;
    item["condition"] = condition;
    item["renderer"] = renderer;
    item["file"] = "screenshots/" + name + ".png";
  }

  int fault(const char* stage) {
    if (!strcmp(stage, "open")) {
      ++opens;
      if (!savingName.empty()) {
        snapshot(savingName, "Saving flight...",
                 "Actual flightTimer_stop frame immediately before JSON staging is opened.",
                 "src/vario/logging/log.cpp:flightTimer_stop");
        savingName.clear();
      }
    }
    if (faultStage == stage && failures) {
      --failures;
      return EIO;
    }
    return 0;
  }
  void clockCapture() {
    if (!shutdownName.empty() && opens == 3 && !shutdownCaptured) {
      // The first delay after three exhausted attempts is the firmware's 1 s failure notice.
      snapshot(
          shutdownName,
          shutdownHasTrack ? "Save failure: IGC unverified" : "Save failure: No IGC file",
          shutdownHasTrack
              ? "Actual shutdown notice with an IGC present; JSON save failed. The track is "
                "explicitly unverified."
              : "Actual shutdown notice with no IGC track; JSON save failed. No acknowledgement.",
          "src/vario/logging/log.cpp:flightTimer_stop");
      shutdownCaptured = true;
    }
  }
  void faults(const char* stage = "", unsigned count = 0) {
    faultStage = stage;
    failures = count;
    opens = 0;
    logbook_io::setFaultHook(fault);
  }
  void units(bool imperial) {
    settings.units_alt = settings.units_climb = settings.units_distance = imperial;
    settings.units_speed = settings.units_hours = imperial;
    settings.units_heading = !imperial;
  }
  void reset() {
    assert(!flightTimer_isRunning());
    modalController.pop_all_pages();
    faults();
    LogbookStore::beginRepairSession(false);
    SD_MMC.simSetPresent(true);
    // A simulated physical removal also unmounts the HAL; restore both layers.
    sdcard.unmount();
    assert(sdcard.mount());
    std::filesystem::remove_all(root + "/logbook");
    std::filesystem::create_directories(root + "/logbook");
    units(true);
    settings.labs_leafLog = true;
  }
  void repair() {
    for (unsigned i = 0; LogbookStore::repairPending() && i < 10000; ++i) {
      LogbookStore::startRepair();
      LogbookStore::serviceRepair();
    }
    assert(!LogbookStore::repairPending());
  }
  FlightStats sampleStats() {
    FlightStats s;
    s.duration = 1458;
    s.gpsalt_start = 832;
    s.gpsalt_end = 517;
    s.gpsalt_min = 517;
    s.gpsalt_max = 1432;
    s.gpsalt_above_launch_max = 600;
    s.climb_max = 420;
    s.climb_min = -310;
    s.speed_max = 12.4f;
    s.distanceStraightLine = 4530;
    s.distanceAlongPath = 12340;
    s.accel_min = .82f;
    s.accel_max = 2.6f;
    s.windValid = true;
    s.windSpeedMax = 7.4f;
    s.windDirectionFromAtMaxDeg = 253;
    return s;
  }
  void startFixture(bool withTrack = false) {
    settings.log_saveTrack = withTrack;
    flightTimer_start();
    logbook = sampleStats();
    logbook.duration = 0;
    logbook.logStartedAt = millis() / 1000;
    logbookEntry.begin(logbook);
    assert(logbookEntry.writePlaceholder(logbook));
    if (withTrack) assert(igcFlight.startFlight());
    sim::clock().advanceUs(1458ULL * 1000000);
    logbook.duration = 1458;
  }
  MenuPage* currentSummary() {
    auto* page = modalController.get_modal_page();
    assert(page && PageFlightSummary::isShowing());
    page->draw();
    return page;
  }
  void syncClock() {
    const std::string body = "GPRMC,184200.00,A,3745.0000,N,12225.0000,W,0.0,0.0,251026,,,A";
    unsigned checksum = 0;
    for (char c : body) checksum ^= c;
    char ending[8];
    snprintf(ending, sizeof(ending), "*%02X\r\n", checksum);
    for (char c : "$" + body + ending) gps.encode(c);
    gps.fixInfo.fix = true;
    assert(gps.syncSystemClock());
  }
  String path(unsigned id) {
    char p[96];
    snprintf(p, sizeof(p), "/logbook/2026-10-25_11-%02u-00_%08x.json", 40 + id, id);
    return p;
  }
  void seed(unsigned id, unsigned revision = 3, bool complete = true, const char* suffix = "",
            const char* status = "", bool legacy = false, bool sparse = false) {
    JsonDocument doc;
    doc["schema"] = "leaf.logbook.flight";
    doc["schema_version"] = "v0.1.0";
    char identity[9];
    snprintf(identity, sizeof(identity), "%08x", id);
    doc["flight_id"] = identity;
    if (!legacy) {
      doc["revision"] = revision;
      doc["complete"] = complete;
    }
    doc["start"]["time_valid"] = !sparse;
    if (!sparse) doc["start"]["time_local"] = "2026-10-25T11:42:00";
    if (complete) doc["end"]["time_valid"] = !sparse;
    const auto stats = sampleStats();
    auto metrics = doc["metrics"].to<JsonObject>();
    metrics["duration_seconds"] = complete ? stats.duration : 1;
    metrics["max_altitude_m"] = stats.gpsalt_max;
    metrics["max_climb_rate_mps"] = 4.2f;
    metrics["max_sink_rate_mps"] = -3.1f;
    metrics["max_ground_speed_mps"] = stats.speed_max;
    metrics["straight_line_distance_m"] = stats.distanceStraightLine;
    metrics["path_distance_m"] = stats.distanceAlongPath;
    if (!sparse) {
      metrics["max_accel_g"] = stats.accel_max;
      metrics["min_accel_g"] = stats.accel_min;
      metrics["max_wind"]["speed_mps"] = stats.windSpeedMax;
      metrics["max_wind"]["direction_from_deg"] = stats.windDirectionFromAtMaxDeg;
    }
    doc["track"]["saved"] = !legacy;
    if (!legacy) {
      doc["track"]["format"] = "igc";
      doc["track"]["path"] = "/tracks/visual-fixture.igc";
    }
    if (!strcmp(status, "uploaded")) doc["leaf_log"]["flight_id"] = "visual-fixture-upload";
    if (!strcmp(status, "rejected")) doc["leaf_log"]["rejected"] = "invalid_igc";
    std::string text;
    serializeJson(doc, text);
    std::ofstream file(root + path(id).c_str() + suffix);
    file << text;
    assert(file.good());
  }
  struct Logbook : PageLogbook {
    void selectDelete(unsigned held = 0) {
      cursor_position = 0;
      if (held) setting_change(Button::CENTER, ButtonEvent::INCREMENTED, held);
    }
    void selectPager() { cursor_position = 1; }
    void older() { setting_change(Button::RIGHT, ButtonEvent::CLICKED, 1); }
  } book;
  void captureBook(const char* name, const char* title, const char* condition) {
    for (unsigned i = 0; i < 10000; ++i) book.service();
    book.draw();
    snapshot(std::string("logbook/") + name, title, condition,
             "src/vario/ui/display/pages/menu/page_logbook.cpp");
  }
  void openBook() {
    book.showNewest();
    for (unsigned i = 0; i < 10000; ++i) book.service();
    book.draw();
  }
  void saveScreens() {
    reset();
    startFixture();
    savingName = "saving/normal";
    faults();
    flightTimer_stop(true);
    currentSummary();
    snapshot("summary/unsynced", "Summary: unknown date/time",
             "Completed save without GPS clock synchronization; Date? / Time? are existing "
             "placeholders.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");

    syncClock();
    reset();
    startFixture();
    faults("open", 2);
    savingName = "saving/retrying";
    flightTimer_stop(true);
    assert(opens == 3);
    auto* summary = currentSummary();
    snapshot("summary/saved-imperial", "Summary: saved (imperial)",
             "Real stop succeeds on its third attempt; the summary has no success/retry notice.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");
    units(false);
    summary->draw();
    snapshot("summary/saved-metric", "Summary: saved (metric)",
             "Same saved flight in metric units.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");
    units(true);
    summary->button_event(Button::DOWN, ButtonEvent::CLICKED, 1);
    summary->draw();
    snapshot("summary/delete-selected", "Summary: Delete selected",
             "Existing hold-to-delete affordance.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");
    summary->button_event(Button::CENTER, ButtonEvent::INCREMENTED, 3);
    summary->draw();
    snapshot("summary/delete-hold", "Summary: delete hold in progress",
             "Three of five hold increments.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");
    faults("delete_main", 1);
    summary->button_event(Button::CENTER, ButtonEvent::INCREMENTED, 5);
    summary->draw();
    snapshot("summary/delete-failed-quiet", "Summary: deletion failed quietly",
             "Injected main-file removal failure. Remaining readable summary stays visible; no "
             "cleanup alert.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");

    reset();
    startFixture();
    faults("open", 3);
    flightTimer_stop(true);
    summary = currentSummary();
    snapshot("summary/save-failed-imperial", "Summary: terminal save failure",
             "All three stop attempts fail. RAM statistics plus Logbook save failed; no "
             "acknowledgement.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");
    units(false);
    summary->draw();
    snapshot("summary/save-failed-metric", "Summary: terminal save failure (metric)",
             "Same terminal failure with metric units and numeric wind direction.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");

    reset();
    startFixture();
    SD_MMC.simSetPresent(false);
    flightTimer_stop(true);
    currentSummary();
    snapshot("summary/save-failed-card-missing", "Summary: card removed before stop",
             "Card removed after the placeholder. Terminal failure retains RAM statistics; no "
             "extra missing-card popup.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");

    reset();
    auto stats = sampleStats();
    stats.logStartedAt = millis() / 1000;
    LogbookEntryFile preview;
    preview.begin(stats);
    LogbookEntrySummary populated;
    preview.summary(stats, populated);
    PageFlightSummary populatedPage;
    populatedPage.show(stats, preview.path(), "", true, &populated);
    populatedPage.draw();
    snapshot("summary/populated-metrics", "Summary: all optional metrics present",
             "Synthetic RAM snapshot with valid g-load and wind. Earlier actual-stop fixtures have "
             "no injected IMU samples.",
             "src/vario/ui/display/pages/dialogs/page_flight_summary.cpp");
    modalController.pop_all_pages();

    for (bool withTrack : {false, true})
      for (bool lowBattery : {false, true}) {
        reset();
        startFixture(withTrack);
        faults("open", 3);
        shutdownCaptured = false;
        shutdownHasTrack = withTrack;
        shutdownName = lowBattery ? "saving/failed-low-battery" : "saving/failed-shutdown";
        if (withTrack) shutdownName += "-with-track";
        sim::clock().setStallHook(clockCapture);
        flightTimer_stop(false, lowBattery ? FlightEndCause::LowBattery : FlightEndCause::PowerOff);
        sim::clock().setStallHook(nullptr);
        assert(shutdownCaptured);
        shutdownName.clear();
      }
  }
  void logbookScreens() {
    reset();
    seed(1);
    seed(2);
    seed(3);
    book.showNewest();
    captureBook("opening", "Opening logbook",
                "Read-only filename scan completed; no implicit repair.");
    LogbookStore::beginRepairSession();
    repair();
    book.showNewest();
    captureBook("normal-imperial", "Logbook: newest of three",
                "Three complete readable flights; no status alert.");
    units(false);
    captureBook("normal-metric", "Logbook: metric", "Same newest entry in metric units.");
    units(true);
    book.selectPager();
    book.older();
    captureBook("pager-middle", "Logbook: middle entry", "Both page arrows and 2/3 position.");
    book.selectDelete();
    captureBook("delete-selected", "Logbook: Delete selected", "Existing hold-to-delete row.");
    book.selectDelete(3);
    captureBook("delete-hold", "Logbook: delete hold in progress",
                "Three of five hold increments.");
    faults("delete_main", 1);
    book.selectDelete(5);
    captureBook("delete-failed-quiet", "Logbook: deletion failed quietly",
                "Injected main-file deletion failure; the same available entry remains, with no "
                "error message.");
    faults();
    book.selectDelete(5);
    captureBook("after-delete", "Logbook: after successful deletion",
                "Remaining adjacent flight and updated 1/2 count.");
    book.selectDelete(5);
    book.selectDelete(5);
    captureBook("after-last-delete", "Logbook: last flight deleted",
                "Normal No flights state; no confirmation popup.");

    reset();
    openBook();
    captureBook("empty", "Logbook: empty", "No saved flights on a readable card.");
    seed(1, 1, false);
    openBook();
    captureBook("placeholder-only", "Logbook: incomplete placeholder unavailable",
                "An incomplete main is counted but shown unavailable, with the Repair Logs hint.");
    reset();
    std::ofstream(root + path(1).c_str()) << "{broken";
    openBook();
    captureBook("unreadable-only", "Logbook: unreadable entry unavailable",
                "A malformed main remains visible without any automatic repair.");
    reset();
    seed(1);
    std::ofstream(root + path(2).c_str()) << "{broken";
    seed(3, 1, false);
    openBook();
    assert(LogbookStore::count() == 3);
    captureBook(
        "mixed-records-filtered", "Logbook: all main filenames counted",
        "One good flight, one malformed record and one placeholder; newest is unavailable at 1/3.");

    reset();
    seed(1);
    seed(2, 2, false);
    seed(2, 3, true, ".tmp");
    book.showNewest();
    captureBook("repair-pending", "Logbook: repair pending",
                "The incomplete main remains unavailable before explicit fixture maintenance.");
    LogbookStore::beginRepairSession();
    repair();
    book.showNewest();
    assert(LogbookStore::count() == 2 && !SD_MMC.exists(path(2) + ".tmp"));
    captureBook(
        "after-repair", "Logbook: recovered completed flight",
        "Explicit maintenance promoted the completed temporary before reopening the browser.");
    reset();
    seed(1);
    seed(2, 2, false);
    seed(2, 3, true, ".tmp");
    faults("promote", 10);
    openBook();
    assert(LogbookStore::count() == 2 && SD_MMC.exists(path(2) + ".tmp"));
    captureBook("repair-failed-quiet", "Logbook: repair failed quietly",
                "The incomplete main remains visible at 1/2, with its temporary preserved.");
    reset();
    seed(1, 3, true, ".bak");
    LogbookStore::beginRepairSession();
    repair();
    openBook();
    captureBook("backup-recovered", "Logbook: recovered backup",
                "Explicit fixture maintenance promoted a backup-only record before browsing.");

    reset();
    seed(1, 3, true);
    seed(1, 3, true, ".tmp", "uploaded");
    openBook();
    assert(SD_MMC.exists(path(1) + ".tmp"));
    captureBook("conflict-quiet", "Logbook: unresolved candidate conflict",
                "Equal-revision completed candidates differ. Readable main stays viewable; upload "
                "is blocked; no alert.");
    for (const char* status : {"", "uploaded", "rejected"}) {
      reset();
      seed(1, 3, true, "", status);
      openBook();
      const std::string key = *status ? status : "not-uploaded";
      captureBook(("leaf-log-" + key).c_str(), ("Logbook: Leaf Log " + key).c_str(),
                  "Existing Leaf Log status icon in the header; no new message or dialog.");
    }
    reset();
    seed(1, 0, true, "", "", true, true);
    openBook();
    captureBook("legacy-missing-data", "Logbook: legacy / missing optional data",
                "Legacy entry without revision metadata, clock, g-load or wind. Existing "
                "Date?/Time?/-- placeholders.");
    reset();
    seed(1);
    JsonDocument wide;
    assert(logbook_io::load(path(1), wide));
    wide["metrics"]["duration_seconds"] = 43215;
    wide["metrics"]["max_altitude_m"] = 3410;
    wide["metrics"]["max_climb_rate_mps"] = 8.4f;
    wide["metrics"]["max_sink_rate_mps"] = -5.8f;
    wide["metrics"]["path_distance_m"] = 123400;
    std::string wideText;
    serializeJson(wide, wideText);
    std::ofstream(root + path(1).c_str()) << wideText;
    openBook();
    captureBook("long-duration-values", "Logbook: long duration / wider values",
                "12-hour flight, five-digit altitude in feet and longer distance/climb values; "
                "unchanged firmware formatting.");
    reset();
    SD_MMC.simSetPresent(false);
    openBook();
    captureBook("card-unavailable", "Logbook: storage unavailable",
                "Absent SD card; Unavailable rather than No flights.");
    SD_MMC.simSetPresent(true);

    reset();
    LogMenuPage logging;
    logging.draw();
    snapshot("context/logging-menu", "Logging settings",
             "Existing Save IGC / AutoStart / AutoStop settings.",
             "src/vario/ui/display/pages/menu/page_menu_log.cpp");
    MainMenuPage main;
    main.draw();
    snapshot("context/main-menu", "Main menu / Logbook entry",
             "Existing entry point for on-device logbook review.",
             "src/vario/ui/display/pages/primary/page_menu_main.cpp");
  }
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  output = argv[1];
  manifest["source_commit"] =
      getenv("LEAF_CAPTURE_COMMIT") ? getenv("LEAF_CAPTURE_COMMIT") : "unspecified";
  manifest["dimensions"] = "96x192";
  manifest["screens"].to<JsonArray>();
  char temporary[] = "/tmp/leaf-sd-dialogs-XXXXXX";
  root = mkdtemp(temporary);
  std::filesystem::create_directories(root + "/logbook");
  std::filesystem::create_directories(root + "/tracks");
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.cardRoot = root;
  options.statePath = root + "/state";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.loadDefaults();
  settings.system_autoOff = 0;
  settings.log_saveTrack = settings.log_autoStart = settings.log_autoStop = false;
  settings.system_timeZone = -420;
  saveScreens();
  logbookScreens();
  std::string text;
  serializeJsonPretty(manifest, text);
  std::ofstream(output + "/manifest-firmware.json") << text << '\n';
  logbook_io::setFaultHook(nullptr);
  LogbookStore::beginRepairSession(false);
  modalController.pop_all_pages();
  std::filesystem::remove_all(root);
  printf("Captured %u SD firmware screens at native 96x192.\n",
         unsigned(manifest["screens"].size()));
}
