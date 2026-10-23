#include "logbook/logbook_io.h"
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <assert.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include "display_capture.h"
#include "logbook/logbook_entry.h"
#include "logbook/logbook_store.h"
#include "logging/log.h"
#include "power.h"
#include "runtime.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/display/pages/dialogs/page_flight_summary.h"
#include "ui/display/pages/menu/page_logbook.h"
#include "ui/settings/settings.h"

extern LogbookEntryFile logbookEntry;

namespace {
  std::string root;
  std::string failStage;
  unsigned failures = 0;
  unsigned calls = 0;
  uint32_t faultDelay = 0;
  int injectedError = EIO;
  const char* crashStage = nullptr;
  int crash(const char* stage) {
    if (!strcmp(stage, crashStage)) _exit(101);
    return 0;
  }
  int inject(const char* stage) {
    if (failStage != stage) return 0;
    ++calls;
    if (faultDelay) sim::clock().advanceUs(uint64_t(faultDelay) * 1000);
    if (!failures) return 0;
    --failures;
    return injectedError;
  }
  void faults(const char* stage = "", unsigned count = 0, int error = EIO, uint32_t delay = 0) {
    failStage = stage;
    failures = count;
    calls = 0;
    injectedError = error;
    faultDelay = delay;
    logbook_io::setFaultHook(inject);
  }
  void raw(const String& path, const std::string& text) {
    std::ofstream file(root + path.c_str());
    file << text;
    assert(file.good());
  }
  bool exists(const String& path) { return std::filesystem::exists(root + path.c_str()); }
  void document(JsonDocument& doc, unsigned revision, bool complete, unsigned duration = 10) {
    doc.clear();
    doc["schema"] = "leaf.logbook.flight";
    doc["schema_version"] = "v0.1.0";
    doc["flight_id"] = "1234abcd";
    if (revision) {
      doc["revision"] = revision;
      doc["complete"] = complete;
    }
    doc["start"]["time_valid"] = false;
    doc["metrics"]["duration_seconds"] = duration;
    doc["track"]["saved"] = false;
    if (complete) {
      doc["end"]["time_valid"] = false;
      doc["end"]["temperature_c"] = 20;
    }
  }
  void candidate(const String& path, unsigned revision, bool complete = true,
                 unsigned duration = 10) {
    JsonDocument doc;
    document(doc, revision, complete, duration);
    std::string text;
    serializeJson(doc, text);
    raw(path, text);
  }
  bool save(const String& path, bool complete = true, unsigned duration = 10) {
    logbook_io::Guard guard;
    assert(guard);
    uint32_t revision;
    uint64_t previous;
    if (!logbook_io::prepare(path, revision, previous)) return false;
    JsonDocument doc;
    document(doc, revision, complete, duration);
    return logbook_io::commit(path, doc, previous);
  }
  unsigned revision(const String& path) {
    JsonDocument doc;
    assert(logbook_io::load(path, doc));
    return doc["revision"] | 0U;
  }
  void reset() {
    faults();
    std::filesystem::remove_all(root + "/logbook");
    std::filesystem::create_directories(root + "/logbook");
  }
  const String path = "/logbook/2026-10-22_12-00-00_1234abcd.json";
  void replacementFailures() {
    for (const char* stage : {"open", "write", "short_write", "flush", "sync", "close",
                              "backup_rotate", "backup", "promote", "read", "allocation"}) {
      reset();
      assert(save(path, false, 0));
      assert(save(path, false, 2));
      faults(stage, 1);
      assert(!save(path, true, 100));
      faults();
      assert(logbook_io::recover(path));
      JsonDocument doc;
      assert(logbook_io::load(path, doc));
      assert((doc["metrics"]["duration_seconds"] | 0U) >= 2);
      assert(save(path, true, 100));
      assert(!exists(path + ".tmp") && exists(path + ".bak"));
      assert(logbook_io::recover(path) && logbook_io::recover(path));
      assert(!exists(path + ".tmp"));
    }
    reset();
    assert(save(path));
    faults("promote", 1);
    assert(!save(path, true, 20));
    faults("promote", 1);  // Recovery promotion itself can fail too; preserve its source.
    assert(!logbook_io::recover(path));
    assert(exists(path + ".tmp"));
    faults();
    assert(logbook_io::recover(path));
  }
  void interruptedProcess() {
    for (const char* stage :
         {"open", "write", "flush", "sync", "close", "backup_rotate", "backup", "promote",
          "after_temp_validation", "after_backup", "after_promote"}) {
      reset();
      assert(save(path, false, 0));
      assert(save(path, false, 2));
      const pid_t child = fork();
      assert(child >= 0);
      if (child == 0) {
        crashStage = stage;
        logbook_io::setFaultHook(crash);
        save(path, true, 100);
        _exit(102);
      }
      int status;
      assert(waitpid(child, &status, 0) == child);
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 101);
      faults();
      assert(logbook_io::recover(path));
      JsonDocument doc;
      assert(logbook_io::load(path, doc));
      assert((doc["metrics"]["duration_seconds"] | 0U) >= 2);
      assert(save(path, true, 100));
      assert(!exists(path + ".tmp"));
    }
  }
  void recoverySets() {
    struct NoMemory : ArduinoJson::Allocator {
      void* allocate(size_t) override { return nullptr; }
      void deallocate(void*) override {}
      void* reallocate(void*, size_t) override { return nullptr; }
    } noMemory;
    reset();
    assert(save(path));
    {
      JsonDocument doc(&noMemory);
      doc["schema"] = "leaf.logbook.flight";
      assert(doc.overflowed());
      logbook_io::Guard transaction;
      assert(!logbook_io::commit(path, doc));
      assert(logbook_io::lastResult().error == ENOMEM && exists(path));
    }
    for (const char* main : {"", "{broken", "{}", "{\"flight_id\":\"foreign\"}"}) {
      for (const char* suffix : {".tmp", ".bak"}) {
        reset();
        raw(path, main);
        candidate(path + suffix, 3);
        assert(logbook_io::recover(path));
        assert(revision(path) == 3);
      }
    }
    for (const char* suffix : {".tmp", ".bak"}) {
      reset();
      candidate(path + suffix, 4);
      String found;
      assert(LogbookStore::count() == 1);
      assert(LogbookStore::newestEntryPath(found) && found == path);
      LogbookEntrySummary summary;
      assert(LogbookStore::readSummary(found, summary));
      assert(revision(path) == 4 && LogbookStore::count() == 1);
    }
    reset();
    candidate(path, 3, false);
    candidate(path + ".tmp", 4, false);
    candidate(path + ".bak", 2, true);
    assert(logbook_io::recover(path));
    assert(revision(path) == 2);
    reset();
    candidate(path, 4);
    candidate(path + ".tmp", 4, true, 20);
    assert(!logbook_io::recover(path));
    assert(exists(path) && exists(path + ".tmp"));
    assert(!save(path));
    reset();
    candidate(path, 0);
    candidate(path + ".tmp", 0, true, 20);
    assert(!logbook_io::recover(path) && exists(path + ".tmp"));
    reset();
    candidate(path, 0);
    assert(save(path));
    assert(save(path, true, 20));
    reset();
    candidate(path, UINT32_MAX);
    assert(!save(path));
    assert(logbook_io::lastResult().error == EOVERFLOW);
    reset();
    candidate(path, 1);
    raw(path + ".tmp", "{}");
    faults("temp_cleanup", 1);
    assert(logbook_io::recover(path));
    assert(exists(path + ".tmp"));
    faults();
    assert(logbook_io::recover(path));
    assert(!exists(path + ".tmp"));
    reset();
    raw(path + ".tmp", "{broken");
    assert(save(path));  // First write failed without a main.
    reset();
    candidate(path + ".tmp", 1);
    raw(path, "{} trailing");
    assert(logbook_io::recover(path));
    for (const char* stage : {"recovery_sync", "recovery_close"}) {
      reset();
      candidate(path + ".tmp", 1);
      faults(stage, 1);
      assert(!logbook_io::recover(path));
      assert(exists(path + ".tmp"));
      faults();
      assert(logbook_io::recover(path));
    }
    reset();
    assert(save(path));
    raw(path + ".tmp", std::string(65537, ' '));
    assert(!logbook_io::recover(path));
    assert(logbook_io::lastResult().error == EFBIG);
    assert(exists(path) && exists(path + ".tmp"));
    reset();
    assert(save(path));
    raw(path + ".tmp",
        "{\"schema\":\"leaf.logbook.flight\",\"schema_version\":\"v999\",\"flight_id\":"
        "\"1234abcd\"}");
    assert(!logbook_io::recover(path));
    assert(logbook_io::lastResult().error == EPROTONOSUPPORT && exists(path + ".tmp"));
    reset();
    candidate(path, 1);
    {
      std::ofstream file(root + path.c_str(), std::ios::app);
      file << " unexpected";
    }
    {
      JsonDocument doc;
      assert(!logbook_io::load(path, doc));
    }
    assert(logbook_io::lastResult().error == EBADMSG);
  }
  void deletionAndRename() {
    for (const char* stage : {"delete_temp", "delete_backup", "delete_main", "delete_track"}) {
      reset();
      candidate(path, 2);
      candidate(path + ".tmp", 3);
      candidate(path + ".bak", 1);
      raw("/tracks/test.igc", "AXXXTEST\n");
      faults(stage, 1);
      assert(!logbook_io::erase(path, "/tracks/test.igc"));
      assert(exists(path));
      faults();
      assert(logbook_io::erase(path, "/tracks/test.igc"));
      assert(!exists(path) && !exists(path + ".tmp") && !exists(path + ".bak"));
      assert(logbook_io::recover(path) && !exists(path));
    }
    reset();
    const String old = "/logbook/unsynced_1_1234abcd.json";
    candidate(old, 2);
    candidate(old + ".bak", 1);
    faults("rename_main", 1);
    assert(!logbook_io::relocate(old, path));
    assert(exists(old) && !exists(old + ".bak"));
    faults();
    assert(logbook_io::relocate(old, path));
    assert(!exists(old) && !exists(old + ".bak") && exists(path));
    // Associated old-name candidates must disappear with an intentional delete too.
    candidate(old + ".tmp", 1, false);
    assert(LogbookStore::deleteEntry(path));
    assert(!exists(path) && !exists(old + ".tmp"));
  }
  void uploadStatus() {
    reset();
    candidate(path, 1, false);
    LeafLogCandidate pending;
    assert(LogbookStore::classifyForLeafLog(path, pending));
    assert(pending.disposition == LeafLogCandidate::Disposition::NotApplicable);
    assert(save(path, true));
    {
      JsonDocument doc;
      assert(logbook_io::load(path, doc));
      doc["notes"]["pilot_notes"] = "Keep these notes";
      std::string text;
      serializeJson(doc, text);
      raw(path, text);
    }
    assert(LogbookStore::recordLeafLogFlightId(path, "delivered"));
    {
      JsonDocument doc;
      assert(logbook_io::load(path, doc));
      assert(doc["complete"].as<bool>());
      assert(!strcmp(doc["notes"]["pilot_notes"], "Keep these notes"));
      assert(!strcmp(doc["leaf_log"]["flight_id"], "delivered"));
    }
  }
  void finalization() {
    settings.log_autoStart = settings.log_autoStop = false;
    settings.log_saveTrack = false;
    for (unsigned failuresExpected : {0U, 1U, 2U, 3U}) {
      reset();
      flightTimer_start();
      faults("open", failuresExpected);
      flightTimer_stop(false);
      assert(calls == (failuresExpected < 3 ? failuresExpected + 1 : 3));
      assert(!flightTimer_isRunning());
      const unsigned previousCalls = calls;
      flightTimer_stop(false);
      assert(calls == previousCalls);
      faults();
      flightTimer_start();
      assert(flightTimer_isRunning());
      flightTimer_stop(false);
    }
    reset();
    flightTimer_start();
    faults("open", 10, ENOSPC);
    flightTimer_stop(false);
    assert(calls == 1);
    reset();
    flightTimer_start();
    faults("open", 10, EIO, 2100);
    flightTimer_stop(false);
    assert(calls == 1);
    // Initial placeholder failure is throttled, but finalization is independent of that cooldown.
    reset();
    LogbookEntryFile entry;
    FlightStats stats;
    assert(entry.begin(stats));
    faults("open", 10);
    assert(!entry.writePlaceholder(stats));
    assert(!entry.writePlaceholder(stats));
    assert(calls == 1);
    faults();
    assert(entry.finalize(stats));
    entry.reset();
    assert(!entry.active());
    // Ordinary and low-battery shutdown share the same bounded save opportunity.
    for (bool lowBattery : {false, true}) {
      reset();
      flightTimer_start();
      faults("open", 2);
      power.shutdown(lowBattery);
      assert(calls == 3 && !flightTimer_isRunning());
      faults();
      assert(power.switchToOnState());
    }
    // Displaying a failed summary cannot secretly start a fourth promotion attempt.
    reset();
    flightTimer_start();
    faults("promote", 10);
    flightTimer_stop(true);
    assert(calls == 3);
    faults();
    // A flight currently being recorded cannot be deleted and later resurrected by a milestone.
    reset();
    flightTimer_start();
    assert(logbookEntry.writePlaceholder(logbook));
    assert(!LogbookStore::deleteEntry(logbookEntry.path()));
    assert(logbook_io::lastResult().error == EBUSY);
    flightTimer_stop(false);
  }
  void latency() {
    reset();
    LogbookEntryFile entry;
    FlightStats stats;
    assert(entry.begin(stats));
    auto start = std::chrono::steady_clock::now();
    assert(entry.writePlaceholder(stats));
    auto placeholder = std::chrono::steady_clock::now();
    stats.duration = 120;
    assert(entry.finalize(stats));
    auto finished = std::chrono::steady_clock::now();
    printf(
        "Host filesystem wall time: placeholder_us=%lld finalization_us=%lld (not device "
        "latency).\n",
        (long long)std::chrono::duration_cast<std::chrono::microseconds>(placeholder - start)
            .count(),
        (long long)std::chrono::duration_cast<std::chrono::microseconds>(finished - placeholder)
            .count());
    entry.reset();
  }
  void fullCard() {
    if (!getenv("LEAF_TEST_FILL_CARD")) return;
    struct statfs filesystem;
    assert(statfs(root.c_str(), &filesystem) == 0 && filesystem.f_type == 0x4d44);
    reset();
    assert(save(path, false, 0));
    const std::string filler = root + "/fill.bin";
    FILE* file = fopen(filler.c_str(), "wb");
    assert(file);
    char buffer[65536] = {};
    while (fwrite(buffer, 1, sizeof(buffer), file) == sizeof(buffer)) {
    }
    assert(ferror(file) && errno == ENOSPC);
    fclose(file);
    assert(!save(path, true, 100));
    assert(logbook_io::lastResult().error == ENOSPC);
    JsonDocument doc;
    assert(logbook_io::load(path, doc));
    assert(!doc["complete"].as<bool>());
    std::filesystem::remove(filler);
    assert(logbook_io::recover(path));
    assert(save(path, true, 100));
    puts("Real FAT32 full-card failure preserved the previous valid record.");
  }
  void ui() {
    const char* output = getenv("LEAF_TEST_SCREENSHOTS");
    if (!output) return;
    std::filesystem::create_directories(output);
    auto screenshot = [&](const char* name) {
      const auto png = sim::encodePng(sim::captureFrame(), 3);
      std::ofstream file(std::string(output) + "/" + name + ".png", std::ios::binary);
      file.write(png.data(), png.size());
      assert(file.good());
    };
    reset();
    candidate(path, 2);
    candidate(path + ".bak", 1);
    struct Summary : PageFlightSummary {
      void attemptDelete() {
        cursor_position = 0;
        setting_change(Button::CENTER, ButtonEvent::INCREMENTED, 5);
      }
      void dismiss() { pop_page(); }
    } summary;
    summary.show(FlightStats(), path, "", false);
    summary.draw();
    screenshot("save-failure");
    faults("delete_backup", 1);
    summary.attemptDelete();
    summary.draw();
    screenshot("summary-delete-failure");
    faults();
    struct Logbook : PageLogbook {
      void attemptDelete() {
        cursor_position = 0;
        setting_change(Button::CENTER, ButtonEvent::INCREMENTED, 5);
      }
    } logbookPage;
    logbookPage.showNewest();
    faults("delete_backup", 1);
    logbookPage.attemptDelete();
    logbookPage.draw();
    screenshot("logbook-delete-failure");
    faults();
    reset();
    raw(path, "{broken");
    logbookPage.showNewest();
    logbookPage.draw();
    screenshot("unavailable-record");
    summary.dismiss();
  }
}  // namespace
int main() {
  char temporary[256];
  const char* cardBase = getenv("LEAF_TEST_CARD_ROOT");
  snprintf(temporary, sizeof(temporary), "%s/leaf-logbook-XXXXXX", cardBase ? cardBase : "/tmp");
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
  settings.system_autoOff = 0;
  replacementFailures();
  interruptedProcess();
  recoverySets();
  deletionAndRename();
  uploadStatus();
  finalization();
  faults();
  flightTimer_stop(false);
  fullCard();
  latency();
  ui();
  faults();
  assert(!logbook_io::erase("/logbook/../profiles/profiles.json"));
  // Ownership cannot change while another thread holds an active logbook transaction.
  {
    logbook_io::Guard held;
    bool accepted = true;
    std::thread other([&] { accepted = sdcard.presentMassStorage(); });
    other.join();
    assert(!accepted);
  }
  std::filesystem::remove_all(root);
  puts("logbook hardening tests passed");
}
