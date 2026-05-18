#include "comms/webserver.h"
#include <WebServer.h>
#include <WiFi.h>
#include "comms/fanet_radio.h"
#include "diagnostics/memory_report.h"
#include "diagnostics/self_test/selfTest.h"
#include "etl/string_stream.h"
#include "power.h"
#include "storage/sd_card.h"
#include "system/version_info.h"
#include "ui/display/display.h"
#include "ui/settings/settings.h"
#include "utils/lock_guard.h"

namespace {
  ::WebServer server;
  String send_buffer = "";
  bool webserver_started = false;

  enum class SelfTestMode { None, Interactive };

  static constexpr uint32_t SELF_TEST_POWER_ON_DELAY_MS = 10000;

  SelfTestMode last_self_test_mode = SelfTestMode::None;
  bool interactive_self_test_pending = false;
  uint32_t interactive_self_test_start_ms = 0;

  const char* selfTestModeName(SelfTestMode mode) {
    switch (mode) {
      case SelfTestMode::Interactive:
        return "interactive";
      case SelfTestMode::None:
      default:
        return "none";
    }
  }

  const char* selfTestStatusName(SelfTest::Status status) {
    switch (status) {
      case SelfTest::Status::Pass:
        return "pass";
      case SelfTest::Status::Fail:
        return "fail";
      case SelfTest::Status::Running:
        return "running";
      case SelfTest::Status::Complete:
        return "complete";
      case SelfTest::Status::Unknown:
      default:
        return "unknown";
    }
  }

  void appendSelfTestResult(String& json, const char* name, SelfTest::Status status,
                            bool trailingComma = true) {
    json += "\"";
    json += name;
    json += "\":\"";
    json += selfTestStatusName(status);
    json += "\"";
    if (trailingComma) json += ",";
  }

  String selfTestSnapshotJson() {
    const bool running = selfTest.updateNeeded();
    String json = "{";
    json += "\"running\":";
    json += (running || interactive_self_test_pending) ? "true" : "false";
    json += ",\"pending\":";
    json += interactive_self_test_pending ? "true" : "false";
    json += ",\"mode\":\"";
    json += selfTestModeName(last_self_test_mode);
    json += "\",\"status\":\"";
    json += interactive_self_test_pending ? "pending"
            : running                     ? "running"
                                          : selfTestStatusName(selfTest.results.allTests);
    json += "\",\"results\":{";
    appendSelfTestResult(json, "sd_card", selfTest.results.sdCard);
    appendSelfTestResult(json, "baro", selfTest.results.baro);
    appendSelfTestResult(json, "imu", selfTest.results.imu);
    appendSelfTestResult(json, "gps", selfTest.results.gps);
    appendSelfTestResult(json, "ambient", selfTest.results.ambient);
    appendSelfTestResult(json, "display", selfTest.results.display);
    appendSelfTestResult(json, "buttons", selfTest.results.buttons);
    appendSelfTestResult(json, "power", selfTest.results.power);
    appendSelfTestResult(json, "speaker", selfTest.results.speaker);
    appendSelfTestResult(json, "vario", selfTest.results.vario);
    appendSelfTestResult(json, "all_tests", selfTest.results.allTests, false);
    json += "}}";
    return json;
  }

  void beginInteractiveSelfTest() {
    interactive_self_test_pending = false;
    last_self_test_mode = SelfTestMode::Interactive;
    selfTest.begin(false);
  }

  void requestInteractiveSelfTest() {
    last_self_test_mode = SelfTestMode::Interactive;

    if (interactive_self_test_pending) return;

    if (power.info().onState == PowerState::OffUSB) {
      display.clear();
      display.showOnSplash();
      display.setPage(MainPage::Thermal);
      power.switchToOnState();
      if (selfTest.updateNeeded()) return;

      interactive_self_test_pending = true;
      interactive_self_test_start_ms = millis();
      return;
    }

    if (selfTest.updateNeeded()) return;

    beginInteractiveSelfTest();
  }

  void updatePendingInteractiveSelfTest() {
    if (!interactive_self_test_pending) return;

    const uint32_t elapsed_ms = millis() - interactive_self_test_start_ms;
    if (elapsed_ms >= SELF_TEST_POWER_ON_DELAY_MS) {
      beginInteractiveSelfTest();
    }
  }
}  // namespace

constexpr auto endl = "\n";

void writeScreenshotBuffer(const char* buffer) {
  Serial.println("Writing screenshot buffer");
  send_buffer += buffer;
}

void webserver_setup() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (webserver_started) return;

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", R"(
      <!DOCTYPE html>
      <html>
        <head>
          <title>ESP32 Vario</title>
          <style>
            body { font-family: Arial, sans-serif; margin: 2em auto; max-width: 800px; }
          </style>
        </head>
        <body>
          <h1>ESP32 Vario Webserver</h1>
          <ul>
            <li><a href="/screenshot" target="_blank">Download Screenshot</a></li>
            <li><a href="/mass_storage" target="_blank">Start Mass Storage</a></li>
            <li><a href="#" onclick="fetch('/self-test/interactive', {method: 'POST'}); return false;">Start Interactive Self Test</a></li>
            <li><a href="#" onclick="fetch('/settings/factory-reset', {method: 'POST'}); return false;">Factory Reset Settings</a></li>
            <li><a href="/mac-address" target="_blank">MAC Address</a></li>
            <li><a href="/firmware-version" target="_blank">Firmware Version</a></li>
            <li><a href="/fanet" target="_blank">FANet Message Stats</a></li>
            <li><a href="/memory" target="_blank">Memory Usage Stats</a></li>
          </ul>
        </body>
      </html>
    )");
  });

  server.on("/raw-xbm", HTTP_GET, []() {
    send_buffer = "";
    u8g2_WriteBufferXBM(u8g2.getU8g2(), writeScreenshotBuffer);
    server.send(200, "image/x-xbitmap", send_buffer);
  });

  server.on("/fanet", HTTP_GET, []() {
    etl::string<1024> str;
    etl::string_stream ss(str);
    auto& radio = fanetRadio;
    auto protocol = radio.protocol;

    // Lock the radio while we poke around their private parts
    LockGuard lock(radio.x_fanet_manager_mutex);
    auto& radioStats = protocol->stats();
    // UnsafeManager* manager = (UnsafeManager*)radio.manager;
    auto ms = millis();

    ss << "<!DOCTYPE html>\n"
       << "<html>\n"
       << "<script>\n"
       << "setTimeout(() => location.reload(), 1000);\n"
       << "</script>\n"
       << "<body><pre>\n"

       << "Current Time: " << ms << endl
       << endl
       << "-- STATS -- " << endl
       << "State: " << fanetRadio.getState().c_str() << "\n"
       << "rx: " << radioStats.rx << "\n"
       << "txSuccess: " << radioStats.txSuccess << "\n"
       << "txFailed: " << radioStats.txFailed << "\n"
       << "processed: " << radioStats.processed << "\n"
       << "forwarded: " << radioStats.forwarded << "\n"
       << "fwdMinRssiDrp: " << radioStats.fwdMinRssiDrp << "\n"
       << "fwdNeighborDrp: " << radioStats.fwdNeighborDrp << "\n"
       << "fwdDbBoostWeak: " << radioStats.fwdDbBoostWeak << "\n"
       << "fwdDbBoostDrop: " << radioStats.fwdDbBoostDrop << "\n"
       << "rxFromUsDrp: " << radioStats.rxFromUsDrp << "\n"
       << "txAck: " << radioStats.txAck << "\n"
       << "neighbors: " << radioStats.neighborTableSize << "\n"
       << "-- Manager --" << endl
       << "Manager Queue Length: " << protocol->txPoolSize() << endl
       << "Next allowed tracking time: "
       << (radio.m_nextAllowedTrackingTimeMs ? (long long)radio.m_nextAllowedTrackingTimeMs - ms
                                             : 0) /
              1000.0f
       << "s" << endl
       << endl

       << "</pre></body>\n"
       << "</html>\n";

    server.send(200, "text/html", str.c_str());
  });

  server.on("/screenshot", HTTP_GET, []() {
    server.send(200, "text/html", R"(
        <!DOCTYPE html>
        <html>
        <head>
        <title>Screenshot</title>
        </head>
        <body>

        <canvas id="screenshotCanvas" width="200" height="200" style="border:0px solid #000;"></canvas>

        <script>
        function drawXBM(xbmData, canvasId) {
          const canvas = document.getElementById(canvasId);
          const ctx = canvas.getContext('2d');

          // Extract width and height (using regex, adjust as needed)
          const widthMatch = xbmData.match(/width (\d+)/);
          const heightMatch = xbmData.match(/height (\d+)/);
          const width = parseInt(widthMatch[1]);
          const height = parseInt(heightMatch[1]);

          // Extract bitmap data (assuming hex format)
          const dataMatch = xbmData.match(/bits\[\] = {(.*?)}/s); // Use /s for multiline matching
          const dataStr = dataMatch[1].replace(/,/g, ''); // Remove commas
          const data = new Uint8Array(dataStr.match(/[\da-f]{2}/gi).map(byte => parseInt(byte, 16)));

          let index = 0;
          for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
              const byteIndex = Math.floor(index / 8);
              const bitIndex = index % 8;
              const pixelValue = (data[byteIndex] >> bitIndex) & 1;

              if (pixelValue) {
                ctx.fillStyle = 'black'; // Or any color you want
              } else {
                ctx.fillStyle = 'white'; // Background color
              }

              // Data is rotated and mirrored, so this should rotate it again
              ctx.fillRect(y, width - x - 1, 1, 1);
              index++;
            }
          }
        }


        fetch('/raw-xbm')
          .then(response => response.text())
          .then(xbmData => {
            drawXBM(xbmData, 'screenshotCanvas');
          })
          .catch(error => {
            console.error('Error fetching XBM data:', error);
          });

        </script>

        </body>
        </html>
    )");
  });

  server.on("/mass_storage", HTTP_GET, []() {
    // Serial.end();
    sdcard.setupMassStorage();
    server.send(200, "text/html", "OK!");
  });

  server.on("/memory", HTTP_GET, []() { server.send(200, "text/plain", getMemoryUsage()); });

  server.on("/mac-address", HTTP_GET, []() {
    String json = "{\"mac_address\":\"";
    json += settings.getMacAddress();
    json += "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/firmware-version", HTTP_GET, []() {
    String json = "{\"firmware_version\":\"";
    json += LeafVersionInfo::firmwareVersion();
    json += "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/settings/factory-reset", HTTP_POST, []() {
    settings.factoryResetVario();
    server.send(200, "application/json", "{\"reset_requested\":true}");
    delay(250);
    ESP.restart();
  });

  server.on("/self-test/interactive", HTTP_POST, []() {
    requestInteractiveSelfTest();
    server.send(200, "application/json", selfTestSnapshotJson());
  });

  server.on("/self-test", HTTP_GET,
            []() { server.send(200, "application/json", selfTestSnapshotJson()); });

  // Give it a chance to settle so the startup message has a valid IP address.
  delay(250);
  // The captive portal belongs on port 80 for setting up WiFi. Keep this
  // webserver on port 81.
  server.begin(81);
  webserver_started = true;
  Serial.printf("Webserver started: http://%s:81/\n", WiFi.localIP().toString());
}

void webserver_loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
    updatePendingInteractiveSelfTest();
  }
}
