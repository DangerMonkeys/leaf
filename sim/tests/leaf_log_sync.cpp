#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "comms/leaf_log_client.h"
#include "comms/leaf_log_credentials.h"
#include "comms/leaf_log_sync.h"
#include "hardware/buttons.h"
#include "runtime.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/settings/settings.h"

namespace sim {
  void setCardRoot(const std::string& root);
}

namespace {
  using Outcome = leaf_log_client::UploadOutcome;
  std::string root;
  std::vector<std::string> attempts;
  std::deque<Outcome> outcomes;
  std::function<void()> duringUpload;
  bool failDirectoryRead = false;
  std::string failReadPath, truncateOnClose;
  unsigned readsBeforeFailure = 0, injectedFailures = 0;

  bool isFile(FILE* file, const std::string& suffix) {
    if (suffix.empty()) return false;
    char link[64], target[1024];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fileno(file));
    const ssize_t length = readlink(link, target, sizeof(target));
    return length >= static_cast<ssize_t>(suffix.size()) &&
           std::string(target + length - suffix.size(), suffix.size()) == suffix;
  }

  void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream file(root + path);
    file << contents;
    assert(file.good());
  }

  void addFlight(const std::string& name, bool delivered = false, bool trackExists = true) {
    writeFile("/logbook/" + name + ".json",
              "{\"schema\":\"leaf.logbook.flight\",\"schema_version\":\"v0.1.0\",\"flight_id\":"
              "\"sim-flight\",\"track\":{\"saved\":true,"
              "\"format\":\"igc\",\"path\":\"/tracks/" +
                  name + ".igc\"}" +
                  (delivered ? ",\"leaf_log\":{\"flight_id\":\"already-uploaded\"}" : "") + "}");
    if (trackExists) writeFile("/tracks/" + name + ".igc", "AXXXTEST\n");
  }

  LeafLogCandidate classify(const std::string& name) {
    LeafLogCandidate candidate;
    const bool classified =
        LogbookStore::classifyForLeafLog(String(("/logbook/" + name + ".json").c_str()), candidate);
    if (!classified) fprintf(stderr, "Classification failed for test flight %s\n", name.c_str());
    assert(classified);
    return candidate;
  }

  void step() {
    leafLogSync.update();
    std::this_thread::yield();
    sim::clock().advanceUs(1000);
  }

  void scan() {
    for (int i = 0; i < 200 && !leafLogSync.progressKnown(); ++i) step();
    assert(leafLogSync.progressKnown());
  }

  void finish() {
    for (int i = 0; i < 100000 && !leafLogSync.massStorageUnavailable(); ++i) step();
    // The emulator declines USB presentation; this is its normal terminal handoff state.
    assert(leafLogSync.massStorageUnavailable());
    assert(!std::filesystem::exists(root + "/.leaf-log-scan.tmp"));
    assert(!std::filesystem::exists(root + "/.leaf-log-pending.tmp"));
  }

  void reset(const std::string& name) {
    failDirectoryRead = false;
    failReadPath.clear();
    truncateOnClose.clear();
    readsBeforeFailure = injectedFailures = 0;
    leafLogSync.prepareForCharging();
    root = root.substr(0, root.find_last_of('/')) + "/" + name;
    std::filesystem::create_directories(root + "/logbook");
    std::filesystem::create_directories(root + "/tracks");
    std::filesystem::create_directories(root + "/state");
    sim::setCardRoot(root);
    SD_MMC.simSetPresent(true);
    sdcard.acquireForFirmwareUse(0, true);
    sdcard.mount();
    Preferences::simSetStatePath(root + "/state");
    assert(leaf_log_credentials::store("llk_sim", "pilot", "Test Pilot"));
    settings.labs_leafLog = 1;
    WiFi.simSetStatus(WL_CONNECTED);
    attempts.clear();
    outcomes.clear();
    duringUpload = {};
    // Reset progress from the previous session before the test checks it.
    step();
  }

  void undatedCleanup() {
    reset("undated-cleanup");
    addFlight("unsynced_5_flight");
    const std::string old = "/logbook/unsynced_5_flight.json";
    std::ifstream in(root + old);
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    writeFile(old + ".bak", bytes);
    bytes.pop_back();
    bytes += ",\"end\":{\"time_valid\":false}}";
    writeFile(old, bytes);
    scan();
    assert(leafLogSync.totalCount() == 1);
    assert(!std::filesystem::exists(root + old));
    assert(!std::filesystem::exists(root + old + ".bak"));
    assert(std::filesystem::exists(root + "/logbook/date_not_available_5_flight.json"));
    finish();
    assert(attempts.size() == 1);
    assert(!std::filesystem::exists(root + "/logbook/date_not_available_5_flight.json.bak"));
    assert(classify("date_not_available_5_flight").disposition ==
           LeafLogCandidate::Disposition::Delivered);
  }
  void trackSizeLimits() {
    reset("track-size-limits");
    addFlight("long-flight");
    const auto path = root + "/tracks/long-flight.igc";
    for (const auto bytes : {5u * 1024 * 1024 + 1, 25u * 1024 * 1024}) {
      std::filesystem::resize_file(path, bytes);
      const auto candidate = classify("long-flight");
      assert(candidate.trackSize == bytes);
      assert(candidate.disposition == LeafLogCandidate::Disposition::Pending);
    }
    std::filesystem::resize_file(path, 25u * 1024 * 1024 + 1);
    const auto candidate = classify("long-flight");
    assert(candidate.disposition == LeafLogCandidate::Disposition::Rejected);
    assert(candidate.rejectionReason == "too_large");
  }

  void largeLogbook() {
    reset("large");
    // beginSession has opened the directory, but has not enumerated it yet.
    for (int i = 0; i < 500; ++i) addFlight("old-" + std::to_string(i), true);
    addFlight("one");
    addFlight("two");
    addFlight("three");
    addFlight("missing", false, false);
    addFlight("also-missing", false, false);
    writeFile("/logbook/malformed.json", "{broken");

    int calls = 0;
    while (!leafLogSync.progressKnown() && calls++ < 100) step();
    assert(leafLogSync.progressKnown() && calls < 50);
    assert(leafLogSync.totalCount() == 3);
    assert(classify("missing").rejectionPersisted);
    assert(classify("also-missing").rejectionPersisted);
    assert(!std::filesystem::exists(root + "/.leaf-log-scan.tmp"));

    // Files added after the snapshot belong to the next session, even after an upload/rejection.
    addFlight("next-session");
    outcomes = {Outcome::Delivered, Outcome::Invalid, Outcome::TooLarge};
    finish();
    assert(attempts.size() == 3);
    assert(std::set<std::string>(attempts.begin(), attempts.end()) ==
           (std::set<std::string>{"one.igc", "two.igc", "three.igc"}));
    assert(classify("next-session").disposition == LeafLogCandidate::Disposition::Pending);
    for (const auto& name : {"one", "two", "three"}) {
      const auto candidate = classify(name);
      assert(candidate.disposition == LeafLogCandidate::Disposition::Delivered ||
             candidate.rejectionPersisted);
    }
    assert(leafLogSync.result() == LeafLogSync::Result::Problem);

    leafLogSync.prepareForCharging();
    sdcard.acquireForFirmwareUse(0, true);
    step();
    scan();
    assert(leafLogSync.totalCount() == 1);
    finish();
    assert(attempts.size() == 4 && attempts.back() == "next-session.igc");
  }

  void retryCurrentFlight() {
    reset("retry");
    addFlight("retry-me");
    addFlight("later");
    outcomes = {Outcome::TransientFailure, Outcome::Delivered, Outcome::Delivered};
    scan();
    for (int i = 0; i < 100000 && !leafLogSync.retryPending(); ++i) step();
    assert(leafLogSync.retryPending() && attempts.size() == 1);
    addFlight("next-session");
    sim::clock().advanceUs(5ULL * 60 * 1000000);
    finish();
    assert(attempts.size() == 3 && attempts[0] == attempts[1] && attempts[2] != attempts[1]);
    assert(leafLogSync.result() == LeafLogSync::Result::Success);
    assert(classify("next-session").disposition == LeafLogCandidate::Disposition::Pending);
  }

  void alreadySyncedAndUnauthorized() {
    reset("already-synced");
    for (int i = 0; i < 500; ++i) addFlight("old-" + std::to_string(i), true);
    scan();
    assert(leafLogSync.totalCount() == 0);
    finish();
    assert(attempts.empty());
    assert(leafLogSync.result() == LeafLogSync::Result::Success);

    reset("unauthorized");
    addFlight("flight");
    outcomes = {Outcome::Unauthorized};
    scan();
    finish();
    assert(attempts.size() == 1);
    assert(!leaf_log_credentials::load().linked());
    assert(leafLogSync.result() == LeafLogSync::Result::Problem);
    assert(classify("flight").disposition == LeafLogCandidate::Disposition::Pending);
  }

  void cancelAndPowerOn() {
    reset("cancel-scan");
    for (int i = 0; i < 100; ++i) addFlight("flight-" + std::to_string(i));
    step();
    assert(leafLogSync.scanning() && !leafLogSync.progressKnown());
    leafLogSync.requestCancel();
    finish();
    assert(attempts.empty());
    assert(leafLogSync.result() == LeafLogSync::Result::Problem);

    reset("power-on");
    addFlight("flight");
    scan();
    leafLogSync.requestPowerOn();
    step();
    assert(leafLogSync.takePowerOnReady());
    assert(!std::filesystem::exists(root + "/.leaf-log-pending.tmp"));
    assert(attempts.empty());

    reset("cancel-upload");
    addFlight("flight");
    outcomes = {Outcome::Cancelled};
    duringUpload = [] { leafLogSync.requestCancel(); };
    scan();
    finish();
    assert(attempts.size() == 1);
    assert(classify("flight").disposition == LeafLogCandidate::Disposition::Pending);
    assert(leafLogSync.result() == LeafLogSync::Result::Problem);
  }

  void queueFailureAndStaleFiles() {
    reset("queue-failure");
    addFlight("flight");
    // A directory at the reserved scratch path forces a queue-open failure.
    leafLogSync.prepareForCharging();
    std::filesystem::create_directory(root + "/.leaf-log-scan.tmp");
    writeFile("/.leaf-log-scan.tmp/block", "keep the directory non-empty");
    step();
    assert(leafLogSync.retryPending() && attempts.empty());
    std::filesystem::remove(root + "/.leaf-log-scan.tmp/block");
    std::filesystem::remove(root + "/.leaf-log-scan.tmp");
    writeFile("/.leaf-log-pending.tmp", "stale partial queue from a previous boot");
    sim::clock().advanceUs(5ULL * 60 * 1000000);
    step();
    scan();
    assert(leafLogSync.totalCount() == 1);
    finish();
    assert(attempts.size() == 1);

    reset("disabled");
    leafLogSync.prepareForCharging();
    settings.labs_leafLog = 0;
    finish();
    assert(attempts.empty());
    assert(leafLogSync.result() == LeafLogSync::Result::None);
  }

  void incompleteScanRecovery() {
    for (const std::string stage : {"directory", "scan-close", "pending-close", "scan-prefix",
                                    "scan-body", "pending-prefix", "pending-body"}) {
      reset("incomplete-" + stage);
      for (int i = 0; i < 3; ++i) addFlight("flight-" + std::to_string(i));
      if (stage == "directory")
        failDirectoryRead = true;
      else if (stage == "scan-close")
        truncateOnClose = "/.leaf-log-scan.tmp";
      else if (stage == "pending-close")
        truncateOnClose = "/.leaf-log-pending.tmp";
      else {
        failReadPath = stage.find("scan-") == 0 ? "/.leaf-log-scan.tmp" : "/.leaf-log-pending.tmp";
        readsBeforeFailure = stage.find("body") != std::string::npos ? 1 : 0;
      }
      for (int i = 0; i < 1000 && !leafLogSync.retryPending(); ++i) step();
      assert(injectedFailures == 1 && leafLogSync.retryPending());
      assert(!leafLogSync.progressKnown() && !leafLogSync.completionPending());
      assert(!leafLogSync.completionNotice() && attempts.empty());
      for (int i = 0; i < 3; ++i)
        assert(classify("flight-" + std::to_string(i)).disposition ==
               LeafLogCandidate::Disposition::Pending);
      sim::clock().advanceUs(5ULL * 60 * 1000000);
      step();
      scan();
      assert(leafLogSync.totalCount() == 3);
      finish();
      assert(attempts.size() == 3 &&
             std::set<std::string>(attempts.begin(), attempts.end()).size() == 3);
    }

    // Rebuild a damaged pending queue even after some uploads have succeeded. Their persisted
    // markers, rather than the discarded scratch list, determine what is left to upload.
    reset("incomplete-after-delivery");
    for (int i = 0; i < 3; ++i) addFlight("flight-" + std::to_string(i));
    scan();
    duringUpload = [] {
      if (attempts.size() == 1) failReadPath = "/.leaf-log-pending.tmp";
    };
    for (int i = 0; i < 10000 && !leafLogSync.retryPending(); ++i) step();
    assert(leafLogSync.retryPending() && injectedFailures == 1 && attempts.size() == 1);
    assert(!leafLogSync.progressKnown() && !leafLogSync.completionPending());
    sim::clock().advanceUs(5ULL * 60 * 1000000);
    step();
    scan();
    assert(leafLogSync.totalCount() == 2);
    finish();
    assert(attempts.size() == 3 &&
           std::set<std::string>(attempts.begin(), attempts.end()).size() == 3);

    // Even an unexpected tail added during the last transfer must not briefly show Complete.
    reset("unexpected-queue-tail");
    addFlight("flight");
    scan();
    duringUpload = [] {
      std::ofstream file(root + "/.leaf-log-pending.tmp", std::ios::app | std::ios::binary);
      file << 'x';
    };
    for (int i = 0; i < 10000 && !leafLogSync.retryPending(); ++i) {
      step();
      assert(!leafLogSync.completionPending() && !leafLogSync.completionNotice());
    }
    assert(leafLogSync.retryPending() && attempts.size() == 1);
    sim::clock().advanceUs(5ULL * 60 * 1000000);
    step();
    scan();
    assert(leafLogSync.totalCount() == 0);
    finish();
    assert(attempts.size() == 1);

    reset("missing-logbook");
    leafLogSync.prepareForCharging();
    std::filesystem::remove(root + "/logbook");
    finish();
    assert(attempts.empty() && !leafLogSync.retryPending());
  }

  void chargingScheduler() {
    reset("scheduler");
    for (int i = 0; i < 500; ++i) addFlight("old-" + std::to_string(i), true);
    leafLogSync.prepareForCharging();
    sim::Runtime::Options options;
    options.cardRoot = root;
    options.statePath = root + "/state";
    options.powerOnAtBoot = false;
    options.speed = 0;
    options.exitOnFatalError = true;
    auto& device = sim::runtime();
    device.configure(options);
    device.boot();
    settings.labs_leafLog = 1;
    assert(leaf_log_credentials::store("llk_sim", "pilot", "Test Pilot"));

    const uint32_t startedMs = millis();
    while (!leafLogSync.completionNotice() && millis() - startedMs < 5000) device.step();
    // Prove that the actual charging scheduler services slices between display ticks. The old
    // one-file-per-500ms schedule would take about 250 seconds for this same card.
    assert(leafLogSync.completionNotice());
    assert(millis() - startedMs < 5000 && attempts.empty());
  }

  void boundedSessionsAndConfirmation() {
    reset("session-limit");
    for (int i = 0; i < 105; ++i) addFlight("flight-" + std::to_string(i));
    // Cleanup continues past the 100-upload session limit.
    const std::string last = "/logbook/flight-104.json";
    std::ifstream input(root + last);
    std::string original((std::istreambuf_iterator<char>(input)), {});
    writeFile(last + ".bak", original);
    original.pop_back();
    original += ",\"end\":{\"time_valid\":false}}";
    writeFile(last, original);
    scan();
    assert(!std::filesystem::exists(root + last + ".bak"));
    assert(leafLogSync.totalCount() == 100);
    finish();
    assert(attempts.size() == 100);
    assert(leafLogSync.result() == LeafLogSync::Result::Success);
    unsigned pending = 0;
    for (int i = 0; i < 105; ++i) {
      if (classify("flight-" + std::to_string(i)).disposition ==
          LeafLogCandidate::Disposition::Pending)
        ++pending;
    }
    assert(pending == 5);
    leafLogSync.prepareForCharging();
    sdcard.acquireForFirmwareUse(0, true);
    step();
    scan();
    assert(leafLogSync.totalCount() == 5);
    finish();
    assert(attempts.size() == 105);

    reset("confirmation-timeout");
    addFlight("flight");
    leafLogSync.requestChargingClick();
    assert(leafLogSync.cancelConfirmation());
    sim::clock().advanceUs(5000000);
    assert(!leafLogSync.cancelConfirmation());
    leafLogSync.requestChargingClick();
    assert(leafLogSync.cancelConfirmation());
    leafLogSync.requestChargingClick();
    finish();
    assert(attempts.empty());
  }

  void unavailableAccountAndCard() {
    reset("unlinked");
    leaf_log_credentials::clear();
    leafLogSync.prepareForCharging();
    finish();
    assert(leafLogSync.result() == LeafLogSync::Result::Problem && attempts.empty());

    reset("missing-card");
    leafLogSync.prepareForCharging();
    sdcard.unmount();
    SD_MMC.simSetPresent(false);
    finish();
    assert(leafLogSync.result() == LeafLogSync::Result::Problem && attempts.empty());
    SD_MMC.simSetPresent(true);
  }
}  // namespace

// Linker wrappers inject errors at the actual filesystem boundary, without firmware hooks.
extern "C" struct dirent* __real_readdir(DIR* directory);
extern "C" struct dirent* __wrap_readdir(DIR* directory) {
  if (failDirectoryRead) {
    failDirectoryRead = false;
    ++injectedFailures;
    errno = EIO;
    return nullptr;
  }
  return __real_readdir(directory);
}
extern "C" size_t __real_fread(void* buffer, size_t size, size_t count, FILE* file);
extern "C" size_t __wrap_fread(void* buffer, size_t size, size_t count, FILE* file) {
  if (isFile(file, failReadPath)) {
    if (readsBeforeFailure)
      --readsBeforeFailure;
    else {
      failReadPath.clear();
      ++injectedFailures;
      errno = EIO;
      return 0;
    }
  }
  return __real_fread(buffer, size, count, file);
}
extern "C" int __real_fclose(FILE* file);
extern "C" int __wrap_fclose(FILE* file) {
  const bool truncate = isFile(file, truncateOnClose);
  const int result = __real_fclose(file);
  if (truncate) {
    const std::string path = root + truncateOnClose;
    truncateOnClose.clear();
    ++injectedFailures;
    // Lose exactly the final complete record during close, leaving a syntactically valid
    // prefix of the queue. Neither a short read nor a malformed length can reveal this loss.
    std::ifstream input(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    size_t position = 0, last = 0;
    while (position < bytes.size()) {
      last = position;
      assert(position + 2 <= bytes.size());
      const size_t length = static_cast<unsigned char>(bytes[position]) |
                            (size_t(static_cast<unsigned char>(bytes[position + 1])) << 8);
      position += 2 + length;
      assert(position <= bytes.size());
    }
    input.close();
    assert(position > 0);
    std::filesystem::resize_file(path, last);
  }
  return result;
}

namespace leaf_log_client {
  UploadResult uploadIgc(const String&, const String& filename, const String& token,
                         std::atomic<bool>&, std::atomic<bool>&) {
    assert(token == "llk_sim");
    attempts.emplace_back(filename.c_str());
    if (duringUpload) duringUpload();
    UploadResult result;
    result.outcome = outcomes.empty() ? Outcome::Delivered : outcomes.front();
    if (!outcomes.empty()) outcomes.pop_front();
    result.flightId = String("uploaded-") + filename;
    return result;
  }
}  // namespace leaf_log_client

int main() {
  char temporary[] = "/tmp/leaf-log-sync-XXXXXX";
  const char* directory = mkdtemp(temporary);
  assert(directory);
  root = std::string(directory) + "/initial";
  sim::clock().setSpeed(0);
  undatedCleanup();
  trackSizeLimits();
  largeLogbook();
  retryCurrentFlight();
  alreadySyncedAndUnauthorized();
  cancelAndPowerOn();
  queueFailureAndStaleFiles();
  incompleteScanRecovery();
  boundedSessionsAndConfirmation();
  unavailableAccountAndCard();
  chargingScheduler();
  leafLogSync.prepareForCharging();
  std::filesystem::remove_all(directory);
  puts("Leaf Log sync regression checks passed.");
}
