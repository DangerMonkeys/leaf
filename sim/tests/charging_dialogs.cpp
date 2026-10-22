#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <assert.h>
#include <sys/time.h>

#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "comms/leaf_log_client.h"
#include "comms/leaf_log_credentials.h"
#include "comms/leaf_log_sync.h"
#include "display_capture.h"
#include "power.h"
#include "runtime.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "sim/usb.h"
#include "storage/sd_card.h"
#include "ui/display/pages/primary/page_charging.h"
#include "ui/settings/settings.h"

namespace sim {
  void setCardRoot(const std::string& root);
}

namespace {
  using Outcome = leaf_log_client::UploadOutcome;
  std::string output;
  std::string root;
  std::atomic<bool> holdUpload{false};
  std::atomic<bool> uploadStarted{false};
  Outcome outcome = Outcome::Delivered;
  unsigned screenshots = 0;

  void step() {
    leafLogSync.update();
    std::this_thread::yield();
    sim::clock().advanceUs(1000);
  }

  void waitFor(const std::function<bool()>& ready) {
    for (unsigned i = 0; i < 1000000 && !ready(); ++i) step();
    assert(ready());
  }

  void scan() {
    waitFor([] { return leafLogSync.progressKnown(); });
  }

  void reset(const char* name, unsigned files = 9, bool usb = true) {
    assert(!leafLogSync.transferInFlight());
    sim::setUsbAcquireBlocked(false);
    sdcard.acquireForFirmwareUse(0, true);
    leafLogSync.prepareForCharging();
    root = "/tmp/leaf-charging-dialogs-" + std::string(name);
    std::filesystem::create_directories(root + "/logbook");
    std::filesystem::create_directories(root + "/tracks");
    std::filesystem::create_directories(root + "/state");
    sim::setCardRoot(root);
    Preferences::simSetStatePath(root + "/state");
    SD_MMC.simSetPresent(true);
    sdcard.mount();
    settings.labs_leafLog = 1;
    settings.dev_mode = 0;
    assert(leaf_log_credentials::store("llk_dialog_test", "pilot", "Test Pilot"));
    sim::setUsbMassStorageAvailable(usb);
    sim::setUsbHostMounted(false);
    WiFi.simSetStatus(WL_CONNECTED);
    timeval tv{1792627200, 0};
    settimeofday(&tv, nullptr);
    holdUpload = false;
    uploadStarted = false;
    outcome = Outcome::Delivered;
    for (unsigned i = 0; i < files; ++i) {
      const auto name = std::to_string(i);
      std::ofstream(root + "/tracks/" + name + ".igc") << "AXXXTEST\n";
      std::ofstream(root + "/logbook/" + name + ".json")
          << "{\"schema\":\"leaf.logbook.flight\",\"track\":{\"saved\":true,\"format\":\"igc\","
             "\"path\":\"/tracks/" +
                 name + ".igc\"}}";
    }
    step();
  }

  void snapshot(const std::string& name) {
    chargingPage_draw();
    auto frame = sim::captureFrame();
    assert(frame.width == 96 && frame.height == 192);
    const auto png = sim::encodePng(frame, 1);
    std::ofstream file(output + "/" + name + ".png", std::ios::binary);
    file.write(png.data(), png.size());
    assert(file.good());
    ++screenshots;
  }

  void assertReferenceFrame(unsigned index, bool dynamicCount = false) {
    // Independent reference frames stay outside firmware. The extractor applies
    // only the approved scan correction, body shift and removal of static counts.
    std::ifstream fixture("docs/ui-reviews/charging-dialog-revamp/reference/expected-dialogs.bin",
                          std::ios::binary);
    std::array<uint8_t, 612> mask{};
    fixture.seekg(index * mask.size());
    fixture.read(reinterpret_cast<char*>(mask.data()), mask.size());
    assert(fixture.good());
    chargingPage_draw();
    const auto frame = sim::captureFrame();
    for (unsigned y = 0; y < 51; ++y)
      for (unsigned x = 0; x < 96; ++x) {
        if (dynamicCount && x >= 54 && x < 94 && y >= 40 && y < 50) continue;
        const bool light = (mask[y * 12 + x / 8] >> (x % 8)) & 1;
        assert(bool(frame.pixels[(141 + y) * 96 + x]) == !light);
      }
  }

  void tap() {
    sim::pressButton("CENTER", true);
    auto started = millis();
    while (millis() - started < 60) sim::runtime().step();
    sim::pressButton("CENTER", false);
    started = millis();
    while (millis() - started < 60) sim::runtime().step();
  }

  void realSchedulerInput() {
    reset("buttons", 9);
    holdUpload = true;
    const auto started = millis();
    while (!uploadStarted.load() && millis() - started < 20000) sim::runtime().step();
    assert(uploadStarted && leafLogSync.transferInFlight());
    tap();
    assert(leafLogSync.cancelConfirmation());
    auto expired = millis();
    while (millis() - expired < 5000) sim::runtime().step();
    assert(!leafLogSync.cancelConfirmation() && leafLogSync.transferInFlight());
    tap();
    assert(leafLogSync.cancelConfirmation());
    tap();
    auto stopped = millis();
    while (!sdcard.hostOwnsMassStorage() && millis() - stopped < 20000) sim::runtime().step();
    assert(sdcard.hostOwnsMassStorage() && !leafLogSync.transferInFlight());
  }

  void referenceAnimations() {
    reset("animations");
    for (unsigned i = 0; i < 7; ++i) {
      assertReferenceFrame(i);
      snapshot("scanning-" + std::to_string(i));
      // Verify the corrected eight-by-two scan bar and exact two-pixel progression.
      const auto frame = sim::captureFrame();
      for (unsigned y = 25; y < 43; ++y)
        for (unsigned x = 17; x < 25; ++x)
          assert(bool(frame.pixels[(141 + y) * 96 + x]) == !(y >= 27 + 2 * i && y < 29 + 2 * i));
      sim::clock().advanceUs(500000);
    }
    scan();
    WiFi.simSetStatus(WL_DISCONNECTED);
    step();
    assert(leafLogSync.screenPhase() == LeafLogSync::ScreenPhase::Wifi);
    for (unsigned i = 0; i < 4; ++i) {
      assertReferenceFrame(7 + i);
      snapshot("wifi-" + std::to_string(i));
      sim::clock().advanceUs(500000);
    }
    WiFi.simSetStatus(WL_CONNECTED);
    timeval tv{0, 0};
    settimeofday(&tv, nullptr);
    step();
    assert(leafLogSync.screenPhase() == LeafLogSync::ScreenPhase::Connecting);
    for (unsigned i = 0; i < 4; ++i) {
      assertReferenceFrame(11 + i);
      snapshot("connecting-" + std::to_string(i));
      sim::clock().advanceUs(500000);
    }
    tv.tv_sec = 1792627200;
    settimeofday(&tv, nullptr);
    step();
    assert(leafLogSync.uploading());
    for (unsigned i = 0; i < 11; ++i) {
      assertReferenceFrame(15 + i, true);
      snapshot("uploading-" + std::to_string(i));
      sim::clock().advanceUs(500000);
    }
    holdUpload = true;
    step();
    waitFor([] { return uploadStarted.load(); });
    chargingPage_button(Button::CENTER, ButtonEvent::CLICKED, 0);
    assert(leafLogSync.cancelConfirmation());
    snapshot("cancel-confirmation");
    sim::clock().advanceUs(5000000);
    assert(!leafLogSync.cancelConfirmation() && leafLogSync.transferInFlight());
    snapshot("cancel-expired");
    chargingPage_button(Button::CENTER, ButtonEvent::CLICKED, 0);
    chargingPage_button(Button::CENTER, ButtonEvent::CLICKED, 0);
    waitFor([] { return sdcard.hostOwnsMassStorage(); });
    snapshot("cancelled-usb-available");
  }

  void completionAndOwnership() {
    reset("complete", 9);
    scan();
    waitFor([] { return leafLogSync.completionNotice(); });
    assertReferenceFrame(26, true);
    snapshot("complete-9-of-9");
    sim::clock().advanceUs(5000000);
    step();
    snapshot("usb-available");
    sim::clock().advanceUs(3600000000ULL);
    step();
    assert(sdcard.hostOwnsMassStorage());
    snapshot("usb-available-after-one-hour");
    sdcard.notifyExplicitEject();
    step();
    scan();
    waitFor([] { return !leafLogSync.screenActive(); });
    assert(!sdcard.hostOwnsMassStorage());
    snapshot("usb-ejected");

    reset("closing", 0);
    scan();
    step();
    assert(sdcard.hostOwnsMassStorage());
    sim::setUsbHostMounted(true);
    sim::setUsbAcquireBlocked(true);
    chargingPage_button(Button::CENTER, ButtonEvent::HELD, 0);
    step();
    assert(leafLogSync.powerOnClosingUsb());
    snapshot("closing-usb");
    sim::setUsbAcquireBlocked(false);
    step();
    assert(leafLogSync.takePowerOnReady());

    reset("turning-on");
    chargingPage_button(Button::CENTER, ButtonEvent::HELD, 0);
    snapshot("turning-on");
    step();
    assert(leafLogSync.takePowerOnReady());

    reset("unavailable", 0, false);
    scan();
    step();
    assert(leafLogSync.massStorageUnavailable());
    snapshot("usb-unavailable");
    sim::setUsbMassStorageAvailable(true);
    chargingPage_button(Button::CENTER, ButtonEvent::CLICKED, 0);
    step();
    assert(sdcard.hostOwnsMassStorage());
  }

  void pausedAndFinishDuringConfirmation() {
    reset("paused");
    outcome = Outcome::TransientFailure;
    scan();
    waitFor([] { return leafLogSync.retryPending(); });
    for (unsigned i = 0; i < 4; ++i) {
      snapshot("paused-" + std::to_string(i));
      const auto frame = sim::captureFrame();
      for (unsigned dot = 0; dot < 3; ++dot)
        for (unsigned y = 174; y < 176; ++y)
          for (unsigned x = 36 + dot * 7; x < 38 + dot * 7; ++x)
            assert(bool(frame.pixels[y * 96 + x]) == !(dot < i));
      sim::clock().advanceUs(500000);
    }
    sim::clock().advanceUs(30000000);
    assert(leafLogSync.screenActive() && !sdcard.hostOwnsMassStorage());
    snapshot("paused-after-30-seconds");
    leafLogSync.requestCancel();
    step();

    reset("finish-confirmation", 1);
    holdUpload = true;
    scan();
    waitFor([] { return uploadStarted.load(); });
    leafLogSync.requestChargingClick();
    assert(leafLogSync.cancelConfirmation());
    holdUpload = false;
    waitFor([] { return leafLogSync.completionNotice(); });
    assert(!leafLogSync.cancelConfirmation());
    snapshot("finished-during-confirmation");

    reset("power-on-during-transfer", 1);
    holdUpload = true;
    scan();
    waitFor([] { return uploadStarted.load(); });
    chargingPage_button(Button::CENTER, ButtonEvent::HELD, 0);
    assert(leafLogSync.powerOnPending());
    snapshot("turning-on-during-upload");
    waitFor([] { return !leafLogSync.transferInFlight(); });
    assert(leafLogSync.takePowerOnReady());
    assert(!leafLogSync.scanning());

    reset("hundred", 100);
    scan();
    assert(leafLogSync.totalCount() == 100);
    waitFor([] { return leafLogSync.completionNotice(); });
    snapshot("complete-100-of-100");
  }

  void missingCardFooter() {
    reset("footer", 0);
    leafLogSync.prepareForCharging();
    snapshot("version-footer");
    SD_MMC.simSetPresent(false);
    sdcard.unmount();
    snapshot("no-sd-card");
  }
}  // namespace

namespace leaf_log_client {
  UploadResult uploadIgc(const String&, const String& filename, const String& token,
                         std::atomic<bool>& cancel, std::atomic<bool>& powerOn) {
    assert(token == "llk_dialog_test");
    uploadStarted.store(true, std::memory_order_release);
    while (holdUpload.load() && !cancel.load() && !powerOn.load()) std::this_thread::yield();
    UploadResult result;
    result.outcome = (cancel.load() || powerOn.load()) ? UploadOutcome::Cancelled : outcome;
    result.flightId = String("test-") + filename;
    return result;
  }
}  // namespace leaf_log_client

int main(int argc, char** argv) {
  output = argc > 1 ? argv[1] : "sim/build/charging-dialogs";
  std::filesystem::create_directories(output);
  sim::Runtime::Options options;
  options.cardRoot = "/tmp/leaf-charging-dialog-boot";
  options.statePath = "/tmp/leaf-charging-dialog-settings";
  options.powerOnAtBoot = false;
  options.speed = 0;
  options.exitOnFatalError = true;
  sim::runtime().configure(options);
  sim::runtime().boot();
  sim::board().setCharging(true);
  power.readBatteryState();
  realSchedulerInput();
  sim::clock().clearTimers();
  referenceAnimations();
  completionAndOwnership();
  pausedAndFinishDuringConfirmation();
  missingCardFooter();
  printf(
      "Charging dialogs passed: exact reference artwork, corrected scan, live input, confirmation, "
      "ownership, completion and %u native 96x192 captures.\n",
      screenshots);
}
