#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <assert.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "comms/fanet_radio.h"
#include "diagnostics/cpu_utilization.h"
#include "display_capture.h"
#include "instruments/gps.h"
#include "logging/buslog.h"
#include "logging/fanet_log.h"
#include "logging/log.h"
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

  void screenshot(Menu& menu, const char* name) {
    menu.draw();
    const auto png = sim::encodePng(sim::captureFrame(), 4);
    std::ofstream out(std::string("sim/build/developer-logging/") + name, std::ios::binary);
    out.write(png.data(), png.size());
    assert(out.good());
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
  menu.press(Button::UP);  // Wrap to bus logger and back.
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
  assert(capture["settings_changed"].as<bool>());
  settings.dev_logFanet = false;
  menu.press(Button::CENTER);
  assert(!cpu_utilization::hasCapture());

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
  puts(
      "Developer logging: runtime STOP/save/retry, radio visibility, flight CSV lifecycle, "
      "GPS validity, packet filtering, persistence, queue overflow and SD recovery passed");
}
