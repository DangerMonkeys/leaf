#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <assert.h>

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

  void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream file(root + path);
    file << contents;
    assert(file.good());
  }

  void addFlight(const std::string& name, bool delivered = false, bool trackExists = true) {
    writeFile("/logbook/" + name + ".json",
              "{\"schema\":\"leaf.logbook.flight\",\"track\":{\"saved\":true,"
              "\"format\":\"igc\",\"path\":\"/tracks/" +
                  name + ".igc\"}" +
                  (delivered ? ",\"leaf_log\":{\"flight_id\":\"already-uploaded\"}" : "") + "}");
    if (trackExists) writeFile("/tracks/" + name + ".igc", "AXXXTEST\n");
  }

  LeafLogCandidate classify(const std::string& name) {
    LeafLogCandidate candidate;
    assert(LogbookStore::classifyForLeafLog(String(("/logbook/" + name + ".json").c_str()),
                                            candidate));
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
    assert(leafLogSync.completionPending());

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
    assert(classify("next-session").disposition == LeafLogCandidate::Disposition::Pending);
  }

  void alreadySyncedAndUnauthorized() {
    reset("already-synced");
    for (int i = 0; i < 500; ++i) addFlight("old-" + std::to_string(i), true);
    scan();
    assert(leafLogSync.totalCount() == 0);
    finish();
    assert(attempts.empty());

    reset("unauthorized");
    addFlight("flight");
    outcomes = {Outcome::Unauthorized};
    scan();
    finish();
    assert(attempts.size() == 1);
    assert(!leaf_log_credentials::load().linked());
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
    while (!leafLogSync.massStorageUnavailable() && millis() - startedMs < 5000) device.step();
    // Prove that the actual charging scheduler services slices between display ticks. The old
    // one-file-per-500ms schedule would take about 250 seconds for this same card.
    assert(leafLogSync.massStorageUnavailable());
    assert(millis() - startedMs < 5000 && attempts.empty());
  }

  void boundedSessionsAndConfirmation() {
    reset("session-limit");
    for (int i = 0; i < 105; ++i) addFlight("flight-" + std::to_string(i));
    scan();
    assert(leafLogSync.totalCount() == 100);
    finish();
    assert(attempts.size() == 100);
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
}  // namespace

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
  largeLogbook();
  retryCurrentFlight();
  alreadySyncedAndUnauthorized();
  cancelAndPowerOn();
  queueFailureAndStaleFiles();
  boundedSessionsAndConfirmation();
  chargingScheduler();
  leafLogSync.prepareForCharging();
  std::filesystem::remove_all(directory);
  puts("Leaf Log sync regression checks passed.");
}
