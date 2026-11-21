// Regression and screenshot capture through real firmware pages and storage decisions.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include "display_capture.h"
#include "logbook/logbook_io.h"
#include "logbook/logbook_store.h"
#include "logging/log.h"
#include "runtime.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/display/pages/menu/page_logbook.h"
#include "ui/display/pages/menu/page_menu_log.h"
#include "ui/display/pages/menu/page_repair_logs.h"
#include "ui/settings/settings.h"

namespace {
  std::string root, output;
  unsigned reads = 0;
  PageRepairLogs controller;
  int countReads(const char* stage) {
    if (!strcmp(stage, "read")) ++reads;
    return 0;
  }
  void advance(unsigned ms) { sim::clock().advanceUs(uint64_t(ms) * 1000); }
  String path(unsigned id) {
    char name[80];
    snprintf(name, sizeof(name), "/logbook/2026-10-26_12-00-00_%08x.json", id);
    return name;
  }
  void seed(unsigned id, unsigned revision = 2, bool complete = true, const char* suffix = "",
            unsigned duration = 20) {
    JsonDocument doc;
    doc["schema"] = "leaf.logbook.flight";
    doc["schema_version"] = "v0.1.0";
    char identity[9];
    snprintf(identity, sizeof(identity), "%08x", id);
    doc["flight_id"] = identity;
    doc["revision"] = revision;
    doc["complete"] = complete;
    doc["start"]["time_valid"] = false;
    doc["metrics"]["duration_seconds"] = duration;
    doc["track"]["saved"] = false;
    if (complete) doc["end"]["time_valid"] = false;
    std::string text;
    serializeJson(doc, text);
    std::ofstream(root + path(id).c_str() + suffix) << text;
  }
  void snapshot(const char* name) {
    auto frame = sim::captureFrame();
    assert(frame.width == 96 && frame.height == 192);
    const auto png = sim::encodePng(frame, 1);
    std::ofstream file(output + "/" + name + ".png", std::ios::binary);
    file.write(png.data(), png.size());
    assert(file.good());
  }
  void reset() {
    SD_MMC.simSetPresent(true);
    controller.pop_all_pages();
    LogbookStore::beginRepairSession(false);
    sdcard.unmount();
    assert(sdcard.mount());
    logbook_io::setFaultHook(nullptr);
    std::filesystem::remove_all(root + "/logbook");
    std::filesystem::create_directories(root + "/logbook");
  }
  void check() {
    for (unsigned i = 0;
         i < 10000 && LogbookStore::repairStatus().state == LogRepairState::Checking; ++i)
      LogbookStore::serviceRepair();
    assert(LogbookStore::repairStatus().state != LogRepairState::Checking);
  }
  void repair() {
    LogbookStore::startRepair();
    for (unsigned i = 0; i < 10000 && LogbookStore::repairPending(); ++i)
      LogbookStore::serviceRepair();
    assert(!LogbookStore::repairPending());
  }
  void dismiss(PageRepairLogs& page) {
    advance(1000);
    page.draw();
    assert(page.get_modal_page() != &page);
  }
  void browsing() {
    reset();
    for (unsigned id = 1; id <= 400; ++id) seed(id);
    seed(1, 1, true, ".bak");
    seed(900, 3, true, ".tmp");
    seed(402, 1, false);
    std::ofstream(root + path(401).c_str()) << "{broken";
    LogbookScan scan;
    static_assert(sizeof(LogbookScan) < 512, "Browse working set must not scale with flight count");
    reads = 0;
    logbook_io::setFaultHook(countReads);
    scan.begin();
    unsigned slices = 0;
    while (scan.pending() && ++slices < 10000) scan.service();
    assert(slices > 20 && slices < 10000 && reads == 0);
    assert(scan.navigation().total == 402 && scan.newestPath() == path(402));
    scan.begin(path(100));
    while (scan.pending()) scan.service();
    assert(reads == 0 && scan.navigation().previousPath == path(99));
    assert(scan.navigation().nextPath == path(101) && scan.navigation().position == 303);
    LogbookEntrySummary summary;
    assert(LogbookStore::readSummary(path(100), summary) && reads == 1);
    assert(!LogbookStore::readSummary(path(900), summary));
    assert(SD_MMC.exists(path(900) + ".tmp") && !SD_MMC.exists(path(900)));
    scan.begin();
    scan.service();
    scan.cancel();
    assert(!scan.pending());
    logbook_io::setFaultHook(nullptr);

    struct Book : PageLogbook {
      void older() {
        cursor_position = 1;
        setting_change(Button::RIGHT, ButtonEvent::CLICKED, 0);
      }
    } book;
    book.showNewest();
    book.draw();
    snapshot("logbook-opening");
    for (unsigned i = 0; i < 100; ++i) book.service();
    book.draw();
    snapshot("logbook-unavailable");
    book.older();
    for (unsigned i = 0; i < 100; ++i) book.service();
    book.draw();
    assert(!SD_MMC.exists(path(900)));
  }
  int temporaryRead(const char* stage) { return !strcmp(stage, "read") ? EIO : 0; }
  void readOutcomes() {
    reset();
    seed(1);
    LogbookEntrySummary summary;
    LogbookReadStatus status;
    assert(LogbookStore::readSummary(path(1), summary, &status) &&
           status == LogbookReadStatus::Ready);
    logbook_io::setFaultHook(temporaryRead);
    assert(!LogbookStore::readSummary(path(1), summary, &status) &&
           status == LogbookReadStatus::Unavailable);
    PageLogbook page;
    page.showNewest();
    while (page.scanning()) page.service();
    page.draw();
    snapshot("logbook-read-retry");
    logbook_io::setFaultHook(nullptr);
    std::atomic<bool> locked{false}, release{false};
    std::thread owner([&] {
      logbook_io::Guard transaction(true);
      locked.store(true);
      while (!release.load()) std::this_thread::yield();
    });
    while (!locked.load()) std::this_thread::yield();
    assert(!LogbookStore::readSummary(path(1), summary, &status) &&
           status == LogbookReadStatus::Unavailable);
    release.store(true);
    owner.join();
    seed(1, 1, false);
    assert(!LogbookStore::readSummary(path(1), summary, &status) &&
           status == LogbookReadStatus::RepairNeeded);
    std::ofstream(root + path(1).c_str()) << "{broken";
    assert(!LogbookStore::readSummary(path(1), summary, &status) &&
           status == LogbookReadStatus::RepairNeeded);
    // Both undated prefixes retain chronological filename-only ordering through migration.
    reset();
    seed(1);
    std::filesystem::rename(root + path(1).c_str(), root + "/logbook/unsynced_10_00000001.json");
    seed(2);
    std::filesystem::rename(root + path(2).c_str(),
                            root + "/logbook/date_not_available_20_00000002.json");
    seed(3);
    LogbookScan scan;
    scan.begin();
    while (scan.pending()) scan.service();
    assert(scan.navigation().total == 3 && scan.newestPath() == path(3));
    scan.begin("/logbook/date_not_available_20_00000002.json");
    while (scan.pending()) scan.service();
    assert(scan.navigation().position == 2 &&
           scan.navigation().previousPath == "/logbook/unsynced_10_00000001.json");
    assert(LogbookStore::beginRepairSession());
    check();
    repair();
    assert(!LogbookStore::repairStatus().unresolved &&
           !SD_MMC.exists("/logbook/unsynced_10_00000001.json"));
  }
  void maintenance() {
    reset();
    std::filesystem::remove_all(root + "/logbook");
    assert(LogbookStore::beginRepairSession());
    assert(LogbookStore::repairStatus().state == LogRepairState::Complete);
    LogbookScan empty;
    empty.begin();
    assert(!empty.pending() && !empty.unavailable());
    reset();
    std::filesystem::remove_all(root + "/logbook");
    std::ofstream(root + "/logbook") << "not a directory";
    assert(LogbookStore::beginRepairSession());
    assert(LogbookStore::repairStatus().state == LogRepairState::Failed);
    empty.begin();
    assert(!empty.pending() && empty.unavailable());
    reset();
    struct Menu : LogMenuPage {
      void select() { cursor_position = 4; }
    } menu;
    menu.select();
    menu.draw();
    snapshot("logging-repair-selected");
    seed(1);
    PageRepairLogs page;
    page.start();
    page.draw();
    snapshot("checking-logs");
    const uint32_t checkingShown = millis();
    check();
    for (unsigned i = 0; i < 20; ++i) page.draw();
    assert(page.get_modal_page() == &page);
    advance(999 - uint32_t(millis() - checkingShown));
    page.draw();
    assert(page.get_modal_page() == &page);
    advance(1);
    page.draw();
    snapshot("no-problems-found");
    const uint32_t resultShown = millis();
    for (unsigned i = 0; i < 20; ++i) page.draw();
    advance(999 - uint32_t(millis() - resultShown));
    page.draw();
    assert(page.get_modal_page() == &page);
    advance(1);
    page.draw();
    assert(page.get_modal_page() != &page);
    assert(!SD_MMC.exists("/.logbook-repair.tmp"));

    reset();
    for (unsigned id = 1; id <= 24; ++id) {
      seed(id, 1, false);
      seed(id, 2, true, ".tmp");
    }
    page.start();
    page.draw();
    check();
    assert(LogbookStore::repairStatus().problems == 24);
    assert(SD_MMC.exists(path(1) + ".tmp"));  // Checking never mutates logbook entries.
    advance(1000);
    page.draw();
    snapshot("repairing-logs");
    repair();
    const auto status = LogbookStore::repairStatus();
    assert(status.state == LogRepairState::Complete && status.repaired == 24 && !status.unresolved);
    for (unsigned id = 1; id <= 24; ++id) assert(!SD_MMC.exists(path(id) + ".tmp"));
    page.draw();  // Fast completion must not shorten the Repairing dialog.
    advance(1000);
    page.draw();
    snapshot("repair-complete");
    dismiss(page);

    reset();
    seed(1, 3, true, "", 20);
    seed(1, 3, true, ".tmp", 30);
    page.start();
    page.draw();
    check();
    advance(1000);
    page.draw();
    repair();
    assert(LogbookStore::repairStatus().unresolved == 1);
    assert(SD_MMC.exists(path(1) + ".tmp"));
    advance(1000);
    page.draw();
    snapshot("repair-incomplete");
    dismiss(page);

    reset();
    SD_MMC.simSetPresent(false);
    page.start();
    page.draw();
    advance(1000);
    page.draw();
    snapshot("repair-unavailable");
    SD_MMC.simSetPresent(true);
    dismiss(page);

    reset();
    seed(1, 1, false);
    seed(1, 2, true, ".tmp");
    page.start();
    page.draw();
    page.button_event(Button::LEFT, ButtonEvent::CLICKED, 0);
    page.draw();
    assert(LogbookStore::repairStatus().state == LogRepairState::Cancelled);
    advance(1000);
    page.draw();
    snapshot("repair-cancelled");
    assert(SD_MMC.exists(path(1) + ".tmp"));
    dismiss(page);

    reset();
    LogbookStore::beginRepairSession();
    SD_MMC.simSetPresent(false);
    LogbookStore::serviceRepair();
    assert(LogbookStore::repairStatus().state == LogRepairState::Failed);
    SD_MMC.simSetPresent(true);

    reset();
    std::atomic<bool> held{false}, release{false};
    std::thread owner([&] {
      logbook_io::Guard lock(true);
      held = true;
      while (!release) std::this_thread::yield();
    });
    while (!held) std::this_thread::yield();
    page.start();
    page.draw();
    snapshot("repair-busy");
    release = true;
    owner.join();
    dismiss(page);

    reset();
    flightTimer_start();
    page.start();
    page.draw();
    snapshot("repair-flight-active");
    dismiss(page);
    flightTimer_stop(false);

    reset();
    seed(1, 1, false);
    seed(1, 2, true, ".tmp");
    LogbookStore::beginRepairSession();
    flightTimer_start();
    LogbookStore::serviceRepair();
    assert(LogbookStore::repairStatus().state == LogRepairState::Cancelled);
    assert(SD_MMC.exists(path(1) + ".tmp"));
    flightTimer_stop(false);
  }
}  // namespace
int main(int argc, char** argv) {
  assert(argc == 2);
  output = argv[1];
  std::filesystem::create_directories(output);
  char temporary[] = "/tmp/leaf-logbook-maintenance-XXXXXX";
  root = mkdtemp(temporary);
  std::filesystem::create_directories(root + "/logbook");
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
  browsing();
  readOutcomes();
  maintenance();
  reset();
  std::filesystem::remove_all(root);
  printf(
      "Read-only sliced browsing, 24 repairs, cancellation, faults and readable dialogs passed; "
      "scan=%zu bytes.\n",
      sizeof(LogbookScan));
}
