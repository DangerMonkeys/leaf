#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <assert.h>
#include <esp_sleep.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "comms/fanet_radio.h"
#include "comms/leaf_log_sync.h"
#include "diagnostics/cpu_utilization.h"
#include "diagnostics/heap_monitor.h"
#include "display_capture.h"
#include "hardware/configuration.h"
#include "instruments/gps.h"
#include "logging/buslog.h"
#include "logging/fanet_log.h"
#include "logging/log.h"
#include "power.h"
#include "runtime.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/display/pages/fanet/page_fanet.h"
#include "ui/display/pages/fanet/page_fanet_ground.h"
#include "ui/display/pages/fanet/page_fanet_ground_select.h"
#include "ui/display/pages/fanet/page_fanet_neighbors.h"
#include "ui/display/pages/fanet/page_fanet_stats.h"
#include "ui/display/pages/menu/page_menu_developer.h"
#include "ui/display/pages/menu/page_menu_settings.h"
#include "ui/display/pages/menu/page_menu_system.h"
#include "ui/settings/settings.h"

namespace {
  class StringPrint : public Print {
   public:
    std::string text;
    size_t write(uint8_t c) override {
      text += static_cast<char>(c);
      return 1;
    }
  };
  const std::string root = "sim/build/developer-logging/card";
  class Menu : public DeveloperMenuPage {
   public:
    int cursor() const { return cursor_position; }
    void press(Button button) { button_event(button, ButtonEvent::CLICKED, 0); }
  };
  class SettingsMenu : public SettingsRootMenuPage {
   public:
    int cursor() const { return cursor_position; }
    void press(Button button) { button_event(button, ButtonEvent::CLICKED, 0); }
  };

  void saveScreen(const char* name) {
    const auto png = sim::encodePng(sim::captureFrame(), 4);
    std::ofstream out(std::string("sim/build/developer-logging/") + name, std::ios::binary);
    out.write(png.data(), png.size());
    assert(out.good());
  }

  class SystemMenu : public SystemMenuPage {
   public:
    void toggleDeveloperMode() {
      for (int i = 0; i < 5; ++i) button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
      button_event(Button::CENTER, ButtonEvent::INCREMENTED, 4);
    }
  };
  void screenshot(Menu& menu, const char* name) {
    menu.draw();
    saveScreen(name);
  }

  std::vector<std::filesystem::path> csvFiles() {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(root + "/fanetlogs")) return result;
    for (const auto& entry : std::filesystem::directory_iterator(root + "/fanetlogs"))
      if (entry.path().extension() == ".csv") result.push_back(entry.path());
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
      const auto sequence = [](const auto& path) {
        const std::string name = path.stem().string();
        return std::stoul(name.substr(name.find_last_of('_') + 1));
      };
      return sequence(a) < sequence(b);
    });
    return result;
  }

  std::vector<std::vector<std::string>> rows(const std::filesystem::path& path) {
    std::ifstream in(path);
    assert(in.good());
    std::string line;
    std::vector<std::vector<std::string>> result;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#' || line.rfind("elapsed_ms", 0) == 0) continue;
      std::vector<std::string> fields;
      std::istringstream stream(line);
      std::string field;
      while (std::getline(stream, field, ',')) fields.push_back(field);
      assert(fields.size() == 10);
      result.push_back(fields);
    }
    return result;
  }

  void runChargeTest(bool eco) {
    settings.system_ecoMode = eco;
    power.shutdown();
    const uint32_t shutdownRequested = millis();
    while (power.info().onState != PowerState::OffUSB && millis() - shutdownRequested < 2000)
      sim::runtime().step();
    assert(power.info().onState == PowerState::OffUSB);
    const uint64_t startedUs = sim::clock().nowUs();
    const uint64_t sleepBefore = sim::clock().lightSleepTotalUs();
    const uint64_t callsBefore = sim::clock().lightSleepCalls();
    while (sim::clock().nowUs() - startedUs < 60'000'000U) sim::runtime().step();
    const uint64_t duration = sim::clock().nowUs() - startedUs;
    const uint64_t asleep = sim::clock().lightSleepTotalUs() - sleepBefore;
    const uint64_t calls = sim::clock().lightSleepCalls() - callsBefore;
    if (eco) {
      assert(asleep * 10 > duration * 9);
      assert(calls >= 118 && calls <= 122);  // 500ms cadence, including time asleep.
    } else {
      assert(calls == 0 && asleep == 0);
    }
    assert(power.info().chargeCurrentMA == 400);
    leafLogSync.requestPowerOn();
    const uint32_t requested = millis();
    while (power.info().onState == PowerState::OffUSB && millis() - requested < 2000)
      sim::runtime().step();
    assert(power.info().onState == PowerState::On);
  }

  void sleepCurrentAveraging() {
    uint16_t milliVolts = 0;
    sim::board().setAdcMilliVolts(ISET, 1100);
    power.readBatteryState();
    assert(sleep_adc::prepare(ISET, 500) == ESP_ERR_NOT_SUPPORTED);
    assert(sleep_adc::finish(false, 0).samples == 0);
    assert(sleep_adc::prepare(ISET, 500'000) == ESP_OK);
    assert(!sleep_adc::readMilliVolts(ISET, milliVolts));
    assert(sleep_adc::finish(false, 0).samples == 0);
    assert(sleep_adc::readMilliVolts(ISET, milliVolts) && milliVolts == 1100);

    assert(sleep_adc::prepare(ISET, 500'000) == ESP_OK);
    sim::board().setAdcMilliVolts(ISET, 1301);
    sim::clock().lightSleepUs(490'000);
    const auto sample = sleep_adc::finish(true, 490'000);
    assert(sample.error == ESP_OK && sample.samples == 1 && sample.milliVolts == 1301);
    sim::board().setAdcMilliVolts(ISET, 1100);
    sim::clock().advanceUs(10'000);
    power.recordSleepCurrent(sample, 490'000);
    power.readBatteryState();
    assert(power.info().chargeCurrentMA >= 470 && power.info().chargeCurrentMA <= 472);

    auto low = sample;
    low.milliVolts = 1100;
    for (unsigned i = 0; i < 8; ++i) {
      sim::clock().advanceUs(500'000);
      power.recordSleepCurrent(low, 490'000);
      power.readBatteryState();
      if (i == 0) assert(power.info().chargeCurrentMA >= 435 && power.info().chargeCurrentMA <= 437);
    }
    assert(power.info().chargeCurrentMA == 400);  // Old samples roll out of the eight-cycle window.
    sim::clock().advanceUs(1'500'000);
    power.recordSleepCurrent(sample, 490'000);
    power.readBatteryState();
    assert(power.info().chargeCurrentMA >= 423 && power.info().chargeCurrentMA <= 425);
    sim::clock().advanceUs(500'000);
    power.readBatteryState();
    assert(power.info().chargeCurrentMA == 400);  // No stale sleep contribution while awake.

    assert(sleep_adc::prepare(ISET, 500'000) == ESP_OK);
    sim::clock().lightSleepUs(1000);
    assert(sleep_adc::finish(true, 1000).samples == 0);
    assert(sleep_adc::readMilliVolts(Power::BATTERY_SENSE_PIN, milliVolts));
  }

  void sentence(const char* body) {
    unsigned checksum = 0;
    for (const char* p = body; *p; ++p) checksum ^= *p;
    char text[100];
    snprintf(text, sizeof(text), "$%s*%02X", body, checksum);
    gps.on_receive(GpsMessage(NMEAString(text)));
  }

  void receive(FanetNeighbors& neighbors, FANET::Address address, bool ground = false,
               bool withPayload = true) {
    const auto type =
        ground ? FANET::Header::MessageType::GROUND_TRACKING : FANET::Header::MessageType::TRACKING;
    FANET::PayloadVariant<FANET_MAX_FRAME_SIZE> payload;
    if (ground) {
      FANET::GroundTrackingPayload tracking;
      tracking.latitude(48.12f);
      tracking.longitude(11.52f);
      payload = tracking;
    } else {
      FANET::TrackingPayload tracking;
      tracking.latitude(48.12f);
      tracking.longitude(11.52f);
      payload = tracking;
    }
    etl::optional<FANET::PayloadVariant<FANET_MAX_FRAME_SIZE>> optionalPayload = payload;
    if (!withPayload) optionalPayload = etl::nullopt;
    FANET::Packet<FANET_MAX_FRAME_SIZE> packet(FANET::Header(false, false, type), address,
                                               etl::nullopt, etl::nullopt, etl::nullopt,
                                               optionalPayload);
    neighbors.on_receive(FanetPacket(packet, -87.5f, 6.25f));
  }
}  // namespace

int main() {
  // Remove only this test's private outputs so repeated runs check exact session counts.
  std::filesystem::remove_all("sim/build/developer-logging");
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.cardRoot = root;
  options.statePath = "sim/build/developer-logging/state";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.loadDefaults();
  assert(settings.system_ecoMode);
  Preferences prefs;
  assert(prefs.begin("varioPrefs", false));
  prefs.remove("ECO_MODE");
  prefs.end();
  settings.system_ecoMode = false;
  settings.retrieve();
  assert(settings.system_ecoMode);  // Missing saved preference uses the ON default.
  settings.dev_mode = true;
  settings.log_autoStart = settings.log_autoStop = settings.log_saveTrack = false;
  assert(!busLog.isLogging());

  Menu menu;
  assert(!fanetRadio.isDetected());
  const auto modalBefore = menu.get_modal_page();
  PageFanet::show();
  PageFanetStats::show();
  PageFanetNeighbors::show();
  PageFanetGroundSelect::show();
  PageFanetGround::show(FanetGroundTrackingMode::WALKING);
  assert(menu.get_modal_page() == modalBefore);
  SettingsMenu settingsMenu;
  settingsMenu.press(Button::UP);  // FANET is the last row; absent units skip it on wrap.
  assert(settingsMenu.cursor() == 8);
  settingsMenu.press(Button::DOWN);
  assert(settingsMenu.cursor() == 0);
  menu.press(Button::DOWN);  // Skips both absent-radio options to Debug Page.
  assert(menu.cursor() == 3);
  screenshot(menu, "no-radio.png");
  menu.press(Button::UP);
  assert(menu.cursor() == 0);
  menu.press(Button::UP);  // Wrap to Detach HW and back.
  assert(menu.cursor() == 8);
  menu.press(Button::DOWN);
  assert(menu.cursor() == 0);
  menu.press(Button::DOWN);
  menu.press(Button::DOWN);
  menu.press(Button::DOWN);  // Runtime row.
  assert(menu.cursor() == 5);
  menu.press(Button::CENTER);
  assert(cpu_utilization::capturing());
  screenshot(menu, "runtime-stop.png");
  sim::clock().advanceUs(100000);
  menu.press(Button::CENTER);  // STOP freezes and exports in one action.
  assert(!cpu_utilization::capturing() && !cpu_utilization::hasCapture());
  assert(SD_MMC.exists(cpu_utilization::exportedPath()));
  menu.press(Button::CENTER);
  assert(cpu_utilization::capturing());
  sdcard.unmount();
  menu.press(Button::CENTER);  // Export fails; preserve the frozen capture.
  assert(!cpu_utilization::capturing() && cpu_utilization::hasCapture());
  screenshot(menu, "runtime-save.png");
  assert(sdcard.mount());
  menu.press(Button::CENTER);  // SAVE retries instead of replacing the capture.
  assert(!cpu_utilization::hasCapture());
  assert(SD_MMC.exists(cpu_utilization::exportedPath()));
  assert(cpu_utilization::start());
  settings.dev_logFanet = true;
  cpu_utilization::freeze();  // Includes captures frozen by a mode change.
  StringPrint frozen;
  assert(cpu_utilization::writeJson(frozen));
  JsonDocument capture;
  assert(!deserializeJson(capture, frozen.text));
  assert(!capture["settings"]["fanet_log"].as<bool>());
  assert(capture["settings"]["route_optimizer"].as<bool>());
  assert(capture["settings_changed"].as<bool>());
  settings.dev_logFanet = false;
  menu.press(Button::CENTER);
  assert(!cpu_utilization::hasCapture());

  menu.press(Button::DOWN);
  assert(menu.cursor() == 6);
  heap_monitor::registerTask("loop", xTaskGetCurrentTaskHandle());
  menu.press(Button::CENTER);
  using StackState = heap_monitor::StackMonitorState;
  assert(heap_monitor::stackMonitorState() == StackState::Running);
  // A pair of FANET workers recreated between periodic samples still contributes.
  for (unsigned i = 0; i < 2; ++i) {
    heap_monitor::registerTask("fanet_rx", xTaskGetCurrentTaskHandle());
    heap_monitor::registerTask("fanet_rx", nullptr);
  }
  sim::clock().advanceUs(3 * 60 * 60 * UINT64_C(1000000));
  assert(!SD_MMC.exists("/diagnostics/stack_1.csv"));
  sdcard.unmount();
  menu.press(Button::CENTER);
  assert(heap_monitor::stackMonitorState() == StackState::Frozen);
  assert(sdcard.mount());
  menu.press(Button::CENTER);
  assert(heap_monitor::stackMonitorState() == StackState::Idle);
  File stackFile = SD_MMC.open("/diagnostics/stack_1.csv");
  assert(stackFile);
  const String stackCsv = stackFile.readString();
  assert(stackCsv.indexOf("loop,10800000,1,4096") >= 0);
  assert(stackCsv.indexOf("fanet_rx,10800000,2,4096") >= 0);
  assert(stackCsv.indexOf("ble,10800000,0,\n") >= 0);
  sim::board().setCharging(true);
  sim::board().setAdcMilliVolts(ISET, 1100);
  menu.press(Button::DOWN);  // Eco Mode follows Stacks.
  assert(menu.cursor() == 7 && settings.system_ecoMode);
  menu.press(Button::CENTER);
  assert(!settings.system_ecoMode);
  settings.save();
  settings.system_ecoMode = true;
  settings.retrieve();
  assert(!settings.system_ecoMode);  // An explicitly saved OFF choice survives retrieval.
  runChargeTest(false);
  menu.press(Button::CENTER);
  assert(settings.system_ecoMode);
  settings.save();
  settings.system_ecoMode = false;
  settings.retrieve();
  assert(settings.system_ecoMode);
  screenshot(menu, "eco-mode.png");
  runChargeTest(true);

  sim::board().fanetPresent = true;
  fanetRadio.setup();
  assert(fanetRadio.isDetected());
  settingsMenu.press(Button::UP);
  assert(settingsMenu.cursor() == 9);
  PageFanet::show();
  assert(menu.get_modal_page() != modalBefore);
  menu.get_modal_page()->button_event(Button::LEFT, ButtonEvent::CLICKED, 0);
  assert(menu.get_modal_page() == modalBefore);
  menu.resetCursor();
  menu.press(Button::DOWN);
  assert(menu.cursor() == 1);
  menu.press(Button::DOWN);
  assert(menu.cursor() == 2);
  menu.press(Button::CENTER);  // Enable while idle: no CSV yet.
  assert(settings.dev_logFanet && !fanet_log::recording() && csvFiles().empty());
  screenshot(menu, "fanet-enabled.png");
  settings.save();
  settings.dev_logFanet = false;
  settings.retrieve();
  assert(settings.dev_logFanet);

  flightTimer_start();
  assert(flightTimer_isRunning() && fanet_log::recording());
  FanetNeighbors neighbors;
  FANET::Address address(0x12, 0x3456);
  FANET::NeighbourTable<FANET::Protocol::FANET_MAX_NEIGHBORS> table;
  table.addOrUpdate(address, millis());
  neighbors.updateFromTable(table);
  receive(neighbors, address);  // No own fix: signal retained, distance/own coordinates blank.
  assert(fanet_log::update());
  sentence("GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  sentence("GPRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,0.0,E");
  gps.update();
  assert(gps.hasUsableFix());
  receive(neighbors, address);
  receive(neighbors, address, true);
  receive(neighbors, address, false, false);  // Invalid tracking payload excluded.
  FANET::Packet<FANET_MAX_FRAME_SIZE> ack(
      FANET::Header(false, false, FANET::Header::MessageType::ACK), address, etl::nullopt,
      etl::nullopt, etl::nullopt, etl::nullopt);
  neighbors.on_receive(FanetPacket(ack, -40, 12));  // No stale-distance row for ACK.
  assert(fanet_log::update());
  const auto first = csvFiles().front();
  auto data = rows(first);
  assert(data.size() == 3);
  assert(std::stoul(data[0][1], nullptr, 16) == address.asUint());
  assert(std::stof(data[0][2]) == -87.5f && std::stof(data[0][3]) == 6.25f);
  assert(data[0][6].empty() && data[0][7].empty() && data[0][8].empty());
  assert(std::stof(data[1][6]) > 0 && std::stod(data[1][7]) > 48);

  receive(neighbors, address);
  menu.press(Button::CENTER);  // Disable flushes the pending observation immediately.
  assert(!settings.dev_logFanet && !fanet_log::recording());
  assert(rows(first).size() == 4);
  receive(neighbors, address);
  assert(fanet_log::update() && rows(first).size() == 4);
  menu.press(Button::CENTER);  // Enable during flight creates a fresh segment.
  assert(fanet_log::recording() && csvFiles().size() == 2);
  receive(neighbors, address);
  flightTimer_stop(false);  // Stop flushes even with IGC recording disabled.
  assert(!fanet_log::recording());
  assert(rows(csvFiles()[1]).size() == 1 && rows(first).size() == 4);
  flightTimer_start();
  assert(csvFiles().size() == 3);
  // A burdened producer cannot block on SD. Overflow is recorded in the CSV.
  std::thread producer([&] {
    for (unsigned i = 0; i < 50; ++i) receive(neighbors, address);
  });
  producer.join();
  assert(fanet_log::update());
  auto files = csvFiles();
  assert(rows(files.back()).size() == 32);
  assert(std::stoul(rows(files.back()).back()[9]) == 18);
  receive(neighbors, address);
  flightTimer_stop(false);
  assert(rows(files.back()).size() == 33);
  assert(!busLog.isLogging() && !std::filesystem::exists(root + "/buslogs"));

  assert(busLog.startLog());
  assert(busLog.startLog() && busLog.isLogging());
  busLog.on_receive(PressureUpdate(millis(), 101325));
  settings.dev_startLogAtBoot = true;
  SystemMenu systemMenu;
  systemMenu.toggleDeveloperMode();
  assert(!settings.dev_mode && !settings.dev_startLogAtBoot && !busLog.isLogging());
  assert(!busLog.startLog());
  const auto logPath = *std::filesystem::directory_iterator(root + "/buslogs");
  std::ifstream busFile(logPath.path());
  std::string line;
  assert(std::getline(busFile, line) && line.front() == 'V');
  assert(std::getline(busFile, line) && line.front() == 'P');
  auto logEnd = std::filesystem::directory_iterator();
  auto logStart = std::filesystem::directory_iterator(root + "/buslogs");
  assert(++logStart == logEnd);

  settings.dev_mode = false;
  flightTimer_start();
  assert(!fanet_log::recording() && csvFiles().size() == 3);
  flightTimer_stop(false);
  settings.dev_mode = true;
  sim::board().fanetPresent = false;
  fanetRadio.setup();
  flightTimer_start();
  assert(!fanet_log::recording() && csvFiles().size() == 3);
  flightTimer_stop(false);
  // Missing storage must not start recording. Restoring the card and toggling the checkbox
  // retries without touching an earlier flight's CSV.
  sim::board().fanetPresent = true;
  fanetRadio.setup();
  sdcard.unmount();
  flightTimer_start();
  assert(!fanet_log::recording() && !fanet_log::update());
  assert(sdcard.mount());
  menu.press(Button::CENTER);
  menu.press(Button::CENTER);
  assert(fanet_log::recording() && csvFiles().size() == 4);
  receive(neighbors, address);
  flightTimer_stop(false);
  assert(rows(csvFiles().back()).size() == 1 && rows(first).size() == 4);
  sleepCurrentAveraging();
  puts(
      "Developer logging: runtime captures, Eco charge sleep, radio visibility, flight CSV lifecycle, "
      "GPS validity, packet filtering, persistence, queue overflow and SD recovery passed");
}
