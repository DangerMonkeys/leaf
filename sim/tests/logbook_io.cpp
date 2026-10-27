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
#include <map>
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
#include "ui/audio/speaker.h"
#include "ui/display/pages/dialogs/page_flight_summary.h"
#include "ui/display/pages/menu/page_logbook.h"
#include "ui/settings/settings.h"

extern LogbookEntryFile logbookEntry;

namespace {
  void serviceMaintenance() {
    LogbookStore::startRepair();
    LogbookStore::serviceRepair();
  }

  std::string root;
  std::string failStage;
  unsigned failures = 0;
  unsigned calls = 0;
  uint32_t faultDelay = 0;
  int injectedError = EIO;
  const char* crashStage = nullptr;
  bool requireMuted = false;
  std::map<std::string, unsigned> stages;
  int crash(const char* stage) {
    if (!strcmp(stage, crashStage)) _exit(101);
    return 0;
  }
  int inject(const char* stage) {
    ++stages[stage];
    if (requireMuted) assert(speaker.muted());
    if (failStage != stage) return 0;
    ++calls;
    if (faultDelay) sim::clock().advanceUs(uint64_t(faultDelay) * 1000);
    if (!failures) return 0;
    --failures;
    return injectedError;
  }
  void faults(const char* stage = "", unsigned count = 0, int error = EIO, uint32_t delay = 0) {
    failStage = stage;
    stages.clear();
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
    LogbookStore::beginRepairSession(false);
    std::filesystem::remove_all(root + "/logbook");
    std::filesystem::create_directories(root + "/logbook");
  }
  const String path = "/logbook/2026-10-22_12-00-00_1234abcd.json";
  void replacementFailures() {
    for (const char* stage :
         {"open", "write", "short_write", "flush", "close", "backup_rotate", "backup", "promote"}) {
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
    for (const char* stage : {"open", "write", "flush", "close", "backup_rotate", "backup",
                              "promote", "after_temp_close", "after_backup", "after_promote"}) {
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
  void interruptedRepair() {
    for (const char* stage : {"backup_rotate", "backup", "repair_after_backup", "promote",
                              "repair_after_promote", "temp_cleanup"}) {
      reset();
      candidate(path, 2, false);
      candidate(path + ".tmp", 3, true, 100);
      candidate(path + ".bak", 1, false);
      if (!strcmp(stage, "temp_cleanup")) {
        candidate(path, 3, true, 100);
        candidate(path + ".tmp", 2, false);
      }
      const pid_t child = fork();
      assert(child >= 0);
      if (child == 0) {
        crashStage = stage;
        logbook_io::setFaultHook(crash);
        logbook_io::recover(path);
        _exit(102);
      }
      int status;
      assert(waitpid(child, &status, 0) == child);
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 101);
      faults();
      assert(logbook_io::recover(path) && revision(path) == 3);
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
      LogbookStore::beginRepairSession();
      for (unsigned i = 0; LogbookStore::repairPending() && i < 1000; ++i) serviceMaintenance();
      assert(!LogbookStore::repairPending());
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
    for (const char* stage : {"promote"}) {
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
  void lightweightSaves() {
    reset();
    FlightStats stats;
    stats.gpsalt_above_launch_max = 7.440002f;
    stats.speed_max = 9.9f;
    LogbookEntryFile entry;
    assert(entry.begin(stats));
    faults("read", 1000);
    assert(entry.writePlaceholder(stats));
    stats.duration = 120;
    assert(entry.finalize(stats));
    LogbookEntrySummary summary;
    entry.summary(stats, summary);
    assert(summary.durationSeconds == 120 && calls == 0);
    faults();
    JsonDocument doc;
    assert(logbook_io::load(entry.path(), doc));
    assert(doc["complete"].as<bool>() && !exists(entry.path() + ".tmp"));

    // A failed placeholder is not retried by ordinary 1 Hz logging updates.
    reset();
    assert(entry.begin(stats));
    faults("open", 10);
    assert(!entry.writePlaceholder(stats));
    sim::clock().advanceUs(60000000);
    assert(!entry.writePlaceholder(stats) && calls == 1);
    faults();
    assert(entry.finalize(stats));

    // A checked temporary is reused across stop-time promotion retries.
    reset();
    assert(entry.begin(stats));
    faults("promote", 2);
    assert(entry.finalize(stats) && calls == 3);
    assert(stages["open"] == 1 && stages["read"] == 0);
    faults();

    // Several launch milestones share one update; later ordinary updates do no I/O.
    reset();
    assert(entry.begin(stats));
    assert(entry.writePlaceholder(stats));
    entry.setTrack("igc", "/tracks/test.igc");
    stats.startLocationLat = 47;
    entry.captureFirstFix(stats);
    assert(entry.writePlaceholder(stats));
    for (int i = 0; i < 60; ++i) assert(entry.writePlaceholder(stats));
    assert(stages["open"] == 2 && stages["read"] == 0);
    assert(entry.finalize(stats));

    // A failed startup staging file never prevents the completed flight save.
    reset();
    assert(entry.begin(stats));
    faults("short_write", 10000);
    assert(!entry.writePlaceholder(stats));
    faults();
    assert(entry.finalize(stats));
    assert(logbook_io::load(entry.path(), doc) && doc["complete"].as<bool>());

    // The actual stop path stays muted throughout storage work, has no JSON reads,
    // and restores an already-muted speaker without enabling it.
    for (bool muted : {false, true}) {
      reset();
      if (muted)
        speaker.mute();
      else
        speaker.unMute();
      flightTimer_start();
      requireMuted = true;
      faults("read", 1000);
      flightTimer_stop(true);
      requireMuted = false;
      assert(calls == 0 && speaker.muted() == muted);
      faults();
      PageFlightSummary page;
      page.pop_all_pages();
    }
    speaker.unMute();
  }
  void quietRepair() {
    reset();
    candidate(path, 2, false);
    candidate(path + ".tmp", 3, true, 90);
    faults("promote", 10);
    LogbookStore::beginRepairSession();
    for (unsigned i = 0; LogbookStore::repairPending() && i < 1000; ++i) serviceMaintenance();
    assert(!LogbookStore::repairPending() && calls == 1 && exists(path + ".tmp"));
    assert(LogbookStore::count() ==
           0);  // Failed promotion left only auxiliary candidates; browsing never repairs.
    LeafLogCandidate upload;
    assert(!LogbookStore::classifyForLeafLog(path, upload) && calls == 2);
    faults();
    LogbookStore::beginRepairSession();
    for (unsigned i = 0; LogbookStore::repairPending() && i < 1000; ++i) serviceMaintenance();
    assert(!LogbookStore::repairPending() && LogbookStore::count() == 1);
    assert(!exists(path + ".tmp") && revision(path) == 3);

    // A new flight abandons maintenance before another file can be read or changed.
    reset();
    candidate(path + ".tmp", 3);
    LogbookStore::beginRepairSession();
    flightTimer_start();
    faults("read", 1000);
    serviceMaintenance();
    assert(!LogbookStore::repairPending() && calls == 0 && exists(path + ".tmp"));
    faults();
    flightTimer_stop(false);

    // A complete main with an ambiguous equal-revision temp may be viewed, but
    // must not be uploaded or overwritten with a result marker.
    reset();
    candidate(path, 3, true, 90);
    candidate(path + ".tmp", 3, true, 100);
    assert(!LogbookStore::classifyForLeafLog(path, upload));
    assert(!LogbookStore::recordLeafLogFlightId(path, "must-not-write"));
    assert(revision(path) == 3 && exists(path + ".tmp"));

    // A redundant temp that cannot be cleaned up is still readable, but must
    // not cause an upload whose result cannot be committed over that temp.
    reset();
    candidate(path, 3, true);
    candidate(path + ".tmp", 2, false);
    faults("temp_cleanup", 10);
    assert(!LogbookStore::classifyForLeafLog(path, upload) && calls == 1);
    assert(!LogbookStore::recordLeafLogFlightId(path, "must-not-write") && calls == 2);
    LogbookEntrySummary readable;
    assert(LogbookStore::readSummary(path, readable));
    faults();
    LogbookStore::beginRepairSession(false);
    assert(LogbookStore::classifyForLeafLog(path, upload) && !exists(path + ".tmp"));

    // Upload preparation also recovers an incomplete main from its completed backup.
    reset();
    candidate(path, 3, false);
    candidate(path + ".bak", 2, true);
    assert(LogbookStore::classifyForLeafLog(path, upload) && revision(path) == 2);
  }
  void fieldEvidence() {
    const char* evidence = getenv("LEAF_FIELD_EVIDENCE");
    if (!evidence) return;
    unsigned devices = 0, repaired = 0;
    for (const auto& directory : std::filesystem::recursive_directory_iterator(evidence)) {
      if (!directory.is_directory() || directory.path().filename() != "logbook") continue;
      reset();
      // The evidence mount is read-only. All recovery operates on disposable copies.
      std::filesystem::copy(directory.path(), root + "/logbook",
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::overwrite_existing);
      std::map<std::string, std::string> completed;
      for (const auto& file : std::filesystem::directory_iterator(root + "/logbook")) {
        if (file.path().extension() != ".tmp") continue;
        const String temporary = String("/logbook/") + file.path().filename().c_str();
        JsonDocument doc;
        if (logbook_io::load(temporary, doc) && doc["complete"].as<bool>()) {
          std::ifstream input(file.path());
          completed[file.path().stem().string()] =
              std::string(std::istreambuf_iterator<char>(input), {});
        }
      }
      LogbookStore::beginRepairSession();
      for (unsigned i = 0; LogbookStore::repairPending() && i < 10000; ++i) serviceMaintenance();
      assert(!LogbookStore::repairPending());
      for (const auto& [filename, original] : completed) {
        std::ifstream input(root + "/logbook/" + filename);
        const std::string current(std::istreambuf_iterator<char>(input), {});
        assert(current == original);
        assert(!std::filesystem::exists(root + "/logbook/" + filename + ".tmp"));
        ++repaired;
      }
      ++devices;
    }
    assert(devices > 0 && repaired > 0);
    printf(
        "Field evidence copies: %u devices, %u completed temporary records recovered unchanged.\n",
        devices, repaired);
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
    logbookPage.draw();
    screenshot("opening-logbook");
    while (LogbookStore::repairPending()) serviceMaintenance();
    logbookPage.draw();
    faults("delete_backup", 1);
    logbookPage.attemptDelete();
    logbookPage.draw();
    screenshot("logbook-delete-failure");
    faults();
    reset();
    raw(path, "{broken");
    logbookPage.showNewest();
    while (LogbookStore::repairPending()) serviceMaintenance();
    logbookPage.draw();
    screenshot("unreadable-record-hidden");
    SD_MMC.simSetPresent(false);
    logbookPage.showNewest();
    while (LogbookStore::repairPending()) serviceMaintenance();
    logbookPage.draw();
    screenshot("card-unavailable");
    SD_MMC.simSetPresent(true);
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
  interruptedRepair();
  recoverySets();
  deletionAndRename();
  uploadStatus();
  finalization();
  faults();
  flightTimer_stop(false);
  fullCard();
  latency();
  lightweightSaves();
  quietRepair();
  fieldEvidence();
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
