// Offline catalog: real firmware sync state machine, page renderer and u8g2 pixels.
// Only upload transport, USB hardware outcomes and the virtual wall clock are controlled.
#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <assert.h>
#include <sys/time.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "comms/leaf_log_client.h"
#include "comms/leaf_log_credentials.h"
#include "comms/leaf_log_sync.h"
#include "display_capture.h"
#include "power.h"
#include "runtime.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/display/pages/primary/page_charging.h"
#include "ui/settings/settings.h"

namespace sim {
  void setCardRoot(const std::string& root);
}
namespace catalog {
  bool usbAvailable = false;
  bool hostMounted = false;
  bool acquireBlocked = false;
}  // namespace catalog

namespace {
  using Outcome = leaf_log_client::UploadOutcome;
  std::string output;
  std::string card;
  std::deque<Outcome> outcomes;
  std::function<void()> duringUpload;
  unsigned captures = 0;

  void snapshot(const char* name) {
    chargingPage_draw();
    const auto frame = sim::captureFrame();
    assert(frame.width == 96 && frame.height == 192);
    std::ofstream file(output + "/" + name + ".png", std::ios::binary);
    const auto png = sim::encodePng(frame, 4);
    file.write(png.data(), png.size());
    assert(file.good());
    ++captures;
    printf("CAPTURE %s | %s\n", name, leafLogSync.statusLine());
  }

  void write(const std::string& path, const std::string& contents) {
    std::ofstream file(card + path);
    file << contents;
    assert(file.good());
  }

  void step() {
    leafLogSync.update();
    sim::clock().advanceUs(1000);
  }

  void wallTime(time_t seconds) {
    timeval tv{seconds, 0};
    assert(settimeofday(&tv, nullptr) == 0);  // Emulator's private clock, not host time.
  }

  void reset(const char* name, unsigned flights = 1, bool usb = false) {
    sim::pressButton("CENTER", false);
    catalog::acquireBlocked = false;
    sdcard.acquireForFirmwareUse(0, true);
    leafLogSync.prepareForCharging();
    card = "/tmp/leaf-dialog-catalog-" + std::string(name);
    std::filesystem::create_directories(card + "/logbook");
    std::filesystem::create_directories(card + "/tracks");
    sim::setCardRoot(card);
    SD_MMC.simSetPresent(true);
    sdcard.mount();
    assert(leaf_log_credentials::store("llk_offline_catalog", "example-pilot", "Example Pilot"));
    settings.labs_leafLog = 1;
    settings.dev_mode = 0;
    WiFi.simSetStatus(WL_CONNECTED);
    wallTime(1792627200);
    catalog::usbAvailable = usb;
    catalog::hostMounted = false;
    outcomes.clear();
    duringUpload = {};
    for (unsigned i = 1; i <= flights; ++i) {
      const auto name = "flight-" + std::to_string(i);
      write("/logbook/" + name + ".json",
            "{\"schema\":\"leaf.logbook.flight\",\"track\":{\"saved\":true,\"format\":\"igc\","
            "\"path\":\"/tracks/" +
                name + ".igc\"}}");
      write("/tracks/" + name + ".igc", "AXXXCATALOG\n");
    }
    step();  // Idle -> eligibility (or USB handoff if disabled/unlinked).
  }

  void scan() {
    for (int i = 0; i < 10000 && !leafLogSync.progressKnown(); ++i) step();
    assert(leafLogSync.progressKnown());
  }

  void waitFor(const std::function<bool()>& condition) {
    for (int i = 0; i < 10000 && !condition(); ++i) step();
    assert(condition());
  }
}  // namespace

namespace leaf_log_client {
  UploadResult uploadIgc(const String&, const String& filename, const String& token,
                         std::atomic<bool>&, std::atomic<bool>&) {
    assert(token == "llk_offline_catalog");
    if (duringUpload) duringUpload();
    UploadResult result;
    result.outcome = outcomes.empty() ? UploadOutcome::Delivered : outcomes.front();
    if (!outcomes.empty()) outcomes.pop_front();
    result.flightId = String("offline-") + filename;
    result.diagnostic = "offline_simulated_outcome";
    return result;
  }
}  // namespace leaf_log_client

int main(int argc, char** argv) {
  assert(argc == 2);
  output = argv[1];
  std::filesystem::create_directories(output);
  sim::Runtime::Options options;
  options.cardRoot = "/tmp/leaf-dialog-catalog-boot";
  options.statePath = "/tmp/leaf-dialog-catalog-settings";
  options.powerOnAtBoot = false;
  options.speed = 0;
  options.exitOnFatalError = true;
  auto& device = sim::runtime();
  device.configure(options);
  device.boot();
  sim::clock().clearTimers();  // Drive each state boundary explicitly; do not miss short states.
  sim::board().setCharging(true);
  power.readBatteryState();

  leafLogSync.prepareForCharging();
  snapshot("00-charging-no-popup");

  reset("normal", 2, true);
  snapshot("01-leaf-log-checking-flights");
  scan();
  WiFi.simSetStatus(WL_DISCONNECTED);
  step();
  assert(std::string(leafLogSync.statusLine()) == "Starting WiFi...");
  snapshot("02-leaf-log-starting-wifi");
  WiFi.simSetStatus(WL_CONNECTED);
  wallTime(0);
  step();
  assert(std::string(leafLogSync.statusLine()) == "Connecting...");
  snapshot("03-leaf-log-connecting-network-time");
  wallTime(1792627200);
  step();
  assert(leafLogSync.uploading());
  snapshot("04-leaf-log-uploading-1-of-2");
  step();
  assert(leafLogSync.preparingNext());
  snapshot("05-leaf-log-preparing-2-of-2");
  step();
  assert(leafLogSync.uploading());
  snapshot("06-leaf-log-uploading-2-of-2");
  step();
  assert(leafLogSync.completionPending());
  snapshot("07-leaf-log-uploaded-2-of-2-preparing-usb");
  step();
  assert(leafLogSync.completionNotice());
  snapshot("08-leaf-log-uploads-complete-usb-ready");
  sim::clock().advanceUs(5000000);
  step();
  assert(!leafLogSync.screenActive());
  snapshot("09-charging-after-completion-popup");

  reset("retry");
  outcomes = {Outcome::TransientFailure};
  scan();
  waitFor([] { return leafLogSync.retryPending(); });
  snapshot("10-leaf-log-upload-paused-retry-scheduled");
  sim::clock().advanceUs(30000000);
  assert(!leafLogSync.screenActive() && leafLogSync.retryPending());
  snapshot("11-charging-retry-pending-popup-hidden");
  sim::clock().advanceUs(270000000);
  step();
  assert(leafLogSync.uploading());  // Same selected flight resumes after first five-minute delay.

  reset("intent");
  sim::pressButton("CENTER", true);
  leafLogSync.requestCancel();
  step();
  assert(std::string(leafLogSync.statusLine()) == "Hold center to turn on");
  snapshot("12-leaf-log-hold-center-to-turn-on");
  sim::pressButton("CENTER", false);
  step();
  assert(leafLogSync.massStorageUnavailable());
  snapshot("13-leaf-log-usb-unavailable-press-to-retry");

  reset("usb-failed-after-upload");
  scan();
  waitFor([] { return leafLogSync.massStorageUnavailable(); });
  assert(leafLogSync.completionPending());
  snapshot("14-leaf-log-uploads-complete-usb-unavailable");
  catalog::usbAvailable = true;
  leafLogSync.requestCancel();
  step();
  assert(leafLogSync.completionNotice());

  reset("power-on");
  leafLogSync.requestPowerOn();
  snapshot("15-turning-on-please-wait");
  step();
  assert(leafLogSync.takePowerOnReady());

  reset("power-on-host", 0, true);
  scan();
  step();
  assert(sdcard.hostOwnsMassStorage());
  catalog::hostMounted = true;
  leafLogSync.requestPowerOn();
  assert(leafLogSync.powerOnClosingUsb());
  catalog::acquireBlocked = true;
  step();
  assert(leafLogSync.powerOnPending());
  snapshot("16-turning-on-closing-usb");
  catalog::acquireBlocked = false;
  step();
  assert(leafLogSync.takePowerOnReady());

  reset("no-card", 0);
  SD_MMC.simSetPresent(false);
  sdcard.unmount();
  leafLogSync.prepareForCharging();
  snapshot("17-charging-no-sd-card-no-popup");

  // Error outcomes have no dedicated message; assert their real transitions as well.
  reset("unauthorized", 1, true);
  scan();
  outcomes = {Outcome::Unauthorized};
  waitFor([] { return !leafLogSync.screenActive(); });
  assert(!leaf_log_credentials::load().linked());
  reset("rejected", 2, true);
  scan();
  outcomes = {Outcome::Invalid, Outcome::TooLarge};
  waitFor([] { return leafLogSync.completionNotice(); });
  assert(leafLogSync.completionPending());

  // Exercise width fallbacks with real queues rather than assigning private state.
  reset("compact", 100, true);
  scan();
  waitFor([] { return leafLogSync.preparingNext() && leafLogSync.currentCount() == 100; });
  snapshot("18-leaf-log-preparing-compact-counter");
  step();
  assert(leafLogSync.uploading());
  snapshot("19-leaf-log-uploading-compact-counter");
  reset("compact-uploaded", 1000, true);
  scan();
  waitFor([] { return leafLogSync.completionPending(); });
  snapshot("20-leaf-log-uploaded-compact-counter");

  reset("verb-only", 10000, true);
  scan();
  for (int i = 0;
       i < 30000 && !(leafLogSync.preparingNext() && leafLogSync.currentCount() == 10000); ++i)
    step();
  assert(leafLogSync.preparingNext() && leafLogSync.currentCount() == 10000);
  snapshot("21-leaf-log-preparing-without-counter");
  step();
  assert(leafLogSync.uploading());
  snapshot("22-leaf-log-uploading-without-counter");
  step();
  assert(leafLogSync.completionPending());
  snapshot("23-leaf-log-uploaded-without-counter");

  printf("Catalog complete: %u screenshots; all flow assertions passed.\n", captures);
}
