#include "comms/webserver.h"
#include <FS.h>
#include <DNSServer.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include "comms/factory_discovery.h"
#include "comms/fanet_radio.h"
#include "comms/wifi_coordinator.h"
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
  ::WebServer user_server(80);
  DNSServer dns_server;
  String send_buffer = "";
  bool webserver_started = false;
  bool user_server_started = false;
  bool user_server_routes_configured = false;
  bool user_app_enabled = false;
  bool user_app_using_leaf_wifi = false;
  bool user_app_provisioning = false;
  String wifi_setup_networks_json = "{\"scanning\":false,\"networks\":[]}";
  bool wifi_setup_scan_running = false;
  bool wifi_setup_connecting = false;
  uint32_t wifi_setup_connect_started_ms = 0;
  String wifi_setup_connect_ssid = "";
  String wifi_setup_connect_error = "";

  enum class SelfTestMode { None, Interactive };

  static constexpr uint32_t SELF_TEST_POWER_ON_DELAY_MS = 10000;
  static constexpr uint32_t WIFI_SETUP_CONNECT_TIMEOUT_MS = 12000;

  SelfTestMode last_self_test_mode = SelfTestMode::None;
  bool interactive_self_test_pending = false;
  uint32_t interactive_self_test_start_ms = 0;

  uint32_t wifi_setup_cycle = 0;
  uint32_t wifi_setup_started_ms = 0;

  void logWifiSetupTiming(const char* event) {
    Serial.printf("Leaf WiFi setup cycle %lu +%lums: %s heap=%u maxAlloc=%u\n",
                  static_cast<unsigned long>(wifi_setup_cycle),
                  static_cast<unsigned long>(millis() - wifi_setup_started_ms),
                  event,
                  ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap());
  }

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
    appendSelfTestResult(json, "gps_serial", selfTest.results.gpsSerial);
    appendSelfTestResult(json, "ambient", selfTest.results.ambient);
    appendSelfTestResult(json, "display", selfTest.results.display);
    appendSelfTestResult(json, "power", selfTest.results.power);
    appendSelfTestResult(json, "vario", selfTest.results.vario);
    appendSelfTestResult(json, "buttons", selfTest.results.buttons);
    appendSelfTestResult(json, "speaker", selfTest.results.speaker);
    appendSelfTestResult(json, "gps_fix", selfTest.results.gpsFix);
    appendSelfTestResult(json, "all_tests", selfTest.results.allTests, false);
    json += "}}";
    return json;
  }

  String latestSelfTestDetailsFileName() {
    String current_file_name = selfTest.resultsFileName();
    if (!current_file_name.isEmpty() && SD_MMC.exists(current_file_name)) {
      return current_file_name;
    }

    String latest_file_name = "";
    char file_name[32];

    for (unsigned int i = 1; i <= 1000; i++) {
      snprintf(file_name, sizeof(file_name), "/self_test_%u.txt", i);
      if (SD_MMC.exists(file_name)) {
        latest_file_name = file_name;
      }
    }

    return latest_file_name;
  }

  void sendLatestSelfTestDetails() {
    if (!sdcard.isMounted()) {
      server.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    String file_name = latestSelfTestDetailsFileName();
    if (file_name.isEmpty()) {
      server.send(404, "application/json", "{\"detail\":\"No self test details file found.\"}");
      return;
    }

    File file = SD_MMC.open(file_name, "r");
    if (!file) {
      server.send(500, "application/json",
                  "{\"detail\":\"Self test details file could not be opened.\"}");
      return;
    }

    server.streamFile(file, "text/plain");
    file.close();
  }

  void clearSelfTestDetailsFiles() {
    if (!sdcard.isMounted()) {
      server.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    if (selfTest.updateNeeded()) {
      server.send(409, "application/json", "{\"detail\":\"Self test is still running.\"}");
      return;
    }

    unsigned int deleted_count = 0;
    unsigned int failed_count = 0;
    char file_name[32];

    for (unsigned int i = 1; i <= 1000; i++) {
      snprintf(file_name, sizeof(file_name), "/self_test_%u.txt", i);
      if (!SD_MMC.exists(file_name)) continue;

      if (SD_MMC.remove(file_name)) {
        deleted_count++;
      } else {
        failed_count++;
      }
    }

    if (failed_count > 0) {
      String json = "{\"cleared\":false,\"deleted_count\":";
      json += deleted_count;
      json += ",\"failed_count\":";
      json += failed_count;
      json += "}";
      server.send(500, "application/json", json);
      return;
    }

    String json = "{\"cleared\":true,\"deleted_count\":";
    json += deleted_count;
    json += "}";
    server.send(200, "application/json", json);
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

  bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }

  bool isValidFanetAddress(const String& fanet_address) {
    if (fanet_address.length() != 6) return false;

    for (size_t i = 0; i < fanet_address.length(); i++) {
      if (!isHexDigit(fanet_address[i])) return false;
    }

    return true;
  }

  String extractJsonStringValue(const String& body, const char* key) {
    String quoted_key = "\"";
    quoted_key += key;
    quoted_key += "\"";

    int key_index = body.indexOf(quoted_key);
    if (key_index < 0) return "";

    int separator_index = body.indexOf(':', key_index + quoted_key.length());
    if (separator_index < 0) return "";

    int value_start = separator_index + 1;
    while (value_start < body.length() && isspace(body[value_start])) {
      value_start++;
    }
    if (value_start >= body.length() || body[value_start] != '"') return "";

    String value;
    for (int i = value_start + 1; i < body.length(); i++) {
      const char c = body[i];
      if (c == '"') break;
      if (c == '\\' && i + 1 < body.length()) {
        i++;
        const char escaped = body[i];
        if (escaped == 'n') {
          value += '\n';
        } else if (escaped == 'r') {
          value += '\r';
        } else {
          value += escaped;
        }
      } else {
        value += c;
      }
    }

    return value;
  }

  bool extractJsonBoolValue(const String& body, const char* key, bool defaultValue = false) {
    String quoted_key = "\"";
    quoted_key += key;
    quoted_key += "\"";

    int key_index = body.indexOf(quoted_key);
    if (key_index < 0) return defaultValue;

    int separator_index = body.indexOf(':', key_index + quoted_key.length());
    if (separator_index < 0) return defaultValue;

    int value_start = separator_index + 1;
    while (value_start < body.length() && isspace(body[value_start])) {
      value_start++;
    }

    if (body.substring(value_start, value_start + 4) == "true") return true;
    if (body.substring(value_start, value_start + 5) == "false") return false;
    return defaultValue;
  }

  String jsonEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 4);
    for (uint16_t i = 0; i < value.length(); i++) {
      const char c = value[i];
      if (c == '"' || c == '\\') escaped += '\\';
      if (c == '\n') {
        escaped += "\\n";
      } else if (c == '\r') {
        escaped += "\\r";
      } else {
        escaped += c;
      }
    }
    return escaped;
  }

  String userAppMode() {
    if (!user_app_enabled) return "off";
    if (user_app_provisioning) return "provisioning";
    if (user_app_using_leaf_wifi) return "leaf_wifi";
    return "network";
  }

  String userAppAddress() {
    if (!user_app_enabled) return "";
    if (user_app_using_leaf_wifi) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
  }

  String userAppUrl() {
    if (!user_app_enabled) return "";
    String url = "http://";
    url += userAppAddress();
    url += "/app";
    return url;
  }

  bool stationConnectionReady() {
    return WiFi.status() == WL_CONNECTED && !WiFi.SSID().isEmpty() &&
           WiFi.localIP() != IPAddress(0, 0, 0, 0);
  }

  void sendNoStoreHeaders(WebServer& target) {
    target.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    target.sendHeader("Pragma", "no-cache");
    target.sendHeader("Expires", "0");
  }

  void stopFailedWifiSetupAttempt(const char* error) {
    wifi_setup_connecting = false;
    wifi_setup_connect_error = error;

    // A failed WiFi.begin() can leave bad setup credentials saved and the STA
    // side retrying. Stop that while keeping the Leaf AP available.
    WiFi.disconnect(false, true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAP("Leaf WiFi");
  }

  String wifiStatusJson() {
    wl_status_t status = WiFi.status();
    if (wifi_setup_connecting && stationConnectionReady()) {
      wifi_setup_connecting = false;
      wifi_setup_connect_error = "";
    } else if (wifi_setup_connecting &&
               millis() - wifi_setup_connect_started_ms > WIFI_SETUP_CONNECT_TIMEOUT_MS) {
      const wl_status_t timed_out_status = WiFi.status();
      if (timed_out_status == WL_CONNECTED) {
        wifi_setup_connecting = false;
        wifi_setup_connect_error = "";
      } else if (timed_out_status == WL_NO_SSID_AVAIL) {
        stopFailedWifiSetupAttempt("network_not_found");
      } else if (timed_out_status == WL_CONNECT_FAILED) {
        stopFailedWifiSetupAttempt("connect_failed");
      } else {
        stopFailedWifiSetupAttempt("timeout");
      }
      status = WiFi.status();
    }

    String json = "{\"status\":";
    json += (int)status;
    json += ",\"connected\":";
    json += status == WL_CONNECTED ? "true" : "false";
    json += ",\"connecting\":";
    json += wifi_setup_connecting ? "true" : "false";
    json += ",\"ssid\":\"";
    json += jsonEscape(WiFi.SSID().isEmpty() && status == WL_CONNECTED ? wifi_setup_connect_ssid : WiFi.SSID());
    json += "\",\"target_ssid\":\"";
    json += jsonEscape(wifi_setup_connect_ssid);
    json += "\",\"ip_address\":\"";
    json += status == WL_CONNECTED ? WiFi.localIP().toString() : "";
    json += "\",\"setup_active\":";
    json += user_app_provisioning ? "true" : "false";
    json += ",\"error\":\"";
    json += jsonEscape(wifi_setup_connect_error);
    json += "\",\"app_url\":\"";
    json += userAppUrl();
    json += "\"}";
    return json;
  }

  void sendRedirect(WebServer& target, const char* location) {
    sendNoStoreHeaders(target);
    target.sendHeader("Location", location, true);
    target.send(302, "text/plain", "");
  }

  bool handleCaptivePortalRequest(WebServer& target) {
    if (!user_app_provisioning) return false;
    sendRedirect(target, "/wifi");
    return true;
  }

  void sendNoCaptivePortalResponse(WebServer& target, const char* body = "") {
    if (handleCaptivePortalRequest(target)) return;
    if (strlen(body) == 0) {
      target.send(204, "text/plain", "");
    } else {
      target.send(200, "text/plain", body);
    }
  }

  void sendUserAppShell(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "text/plain", "Leaf Web App is not active.");
      return;
    }

    target.send(200, "text/html", R"(
      <!DOCTYPE html>
      <html lang="en">
        <head>
          <meta charset="utf-8">
          <meta name="viewport" content="width=device-width, initial-scale=1">
          <title>Leaf Web App</title>
          <style>
            :root {
              color-scheme: light;
              font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
              line-height: 1.35;
            }
            body {
              margin: 0;
              background: #f5f7f2;
              color: #172016;
            }
            header {
              background: #172016;
              color: white;
              padding: 20px;
            }
            main {
              max-width: 720px;
              margin: 0 auto;
              padding: 20px;
            }
            h1 {
              font-size: 28px;
              margin: 0;
            }
            h2 {
              font-size: 18px;
              margin: 0 0 8px;
            }
            section {
              border-bottom: 1px solid #d9dfd3;
              padding: 18px 0;
            }
            label {
              display: block;
              font-size: 14px;
              font-weight: 650;
              margin: 14px 0 6px;
            }
            input, select, button {
              box-sizing: border-box;
              width: 100%;
              border: 1px solid #bac3b4;
              border-radius: 6px;
              font: inherit;
              padding: 12px;
            }
            button {
              background: #172016;
              color: white;
              font-weight: 700;
              margin-top: 16px;
            }
            .status {
              display: grid;
              gap: 6px;
              font-family: ui-monospace, "SFMono-Regular", Consolas, monospace;
              font-size: 14px;
            }
            .muted {
              color: #596256;
            }
          </style>
        </head>
        <body>
          <header>
            <h1>Leaf Web App</h1>
          </header>
          <main>
            <section>
              <h2>Status</h2>
              <div class="status" id="status">Loading...</div>
            </section>
            <section>
              <h2>Settings</h2>
              <p class="muted">Settings tools will be added here.</p>
            </section>
            <section>
              <h2>Flight Logs</h2>
              <p class="muted">Logbook tools will be added here.</p>
            </section>
            <section>
              <h2>Waypoints & Routes</h2>
              <p class="muted">Navigation file tools will be added here.</p>
            </section>
          </main>
          <script>
            async function loadStatus() {
              const target = document.getElementById('status');
              try {
                const response = await fetch('/api/user/status');
                const status = await response.json();
                target.innerHTML = [
                  `mode: ${status.mode}`,
                  `ssid: ${status.ssid || '(none)'}`,
                  `ip: ${status.ip_address || '(none)'}`,
                  `firmware: ${status.firmware_version}`
                ].join('<br>');
              } catch (error) {
                target.textContent = 'Unable to read status.';
              }
            }
            loadStatus();
          </script>
        </body>
      </html>
    )");
  }

  void sendWifiSetupPage(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "text/plain", "Leaf Web App is not active.");
      return;
    }

    sendNoStoreHeaders(target);
    static constexpr char WIFI_SETUP_PAGE[] = R"leafhtml(<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><meta http-equiv=Cache-Control content=no-store><title>Leaf WiFi</title><style>body{margin:0;background:#f5f7f2;color:#172016;font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}h1{margin:0;padding:18px 20px;background:#172016;color:white;font-size:24px}main{padding:20px;max-width:560px;margin:auto}label{display:block;font-weight:650;margin:14px 0 6px}input,select,button{box-sizing:border-box;width:100%;font:inherit;padding:12px;border:1px solid #bac3b4;border-radius:6px}button{margin-top:16px;background:#172016;color:white;font-weight:700}.status{border-bottom:1px solid #d9dfd3;color:#596256;padding-bottom:14px}.row{display:flex;gap:10px}.row input{flex:1}.show{display:flex;align-items:center;gap:6px;width:auto;white-space:nowrap}.show input{width:auto}</style><script>async function init(){let s=document.getElementById('s'),l=document.getElementById('l'),n=document.getElementById('n'),p=document.getElementById('p'),w=document.getElementById('w'),f=document.getElementById('f');l.onchange=()=>{if(l.value)n.value=l.value};w.onchange=()=>p.type=w.checked?'text':'password';async function nets(){try{let d=await(await fetch('/api/wifi/networks')).json();if(d.scanning){s.textContent='Scanning for networks...';setTimeout(nets,1500);return}l.innerHTML='<option value="">Select network...</option>';d.networks.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.textContent=x.ssid+' ('+x.rssi+' dBm)';l.appendChild(o)});s.textContent=d.networks.length?'Choose a network or type one manually.':'No networks found. Type the network name manually.'}catch(e){s.textContent='Unable to read network list. Type the network name manually.'}}async function poll(){try{let d=await(await fetch('/api/wifi/status')).json();if(d.connected){s.textContent='Connected to '+d.ssid+'. Open '+d.ip_address+':81/app';return}if(d.error){s.textContent='Unable to connect. Check password and try again.';return}s.textContent=d.target_ssid?'Trying to connect to '+d.target_ssid+'...':'Trying to connect...';setTimeout(poll,1500)}catch(e){s.textContent='Trying to connect...';setTimeout(poll,2000)}}f.onsubmit=async e=>{e.preventDefault();let ssid=n.value.trim();if(!ssid){s.textContent='Enter a network name.';return}s.textContent='Saving credentials...';try{await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:p.value})});poll()}catch(x){s.textContent='Unable to save network details. Try again.'}};nets()}</script></head><body onload=init()><h1>Leaf WiFi Setup</h1><main><p class=status id=s>Loading...</p><form id=f><label>Network</label><select id=l><option>Select network...</option></select><input id=n autocomplete=off placeholder="Type network name"><label>Password</label><div class=row><input id=p type=password autocomplete=current-password><label class=show><input id=w type=checkbox>Show</label></div><button>Save and Connect</button></form></main></body></html>)leafhtml";
    target.send(200, "text/html", WIFI_SETUP_PAGE);
  }

  void sendUserStatus(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"active\":false}");
      return;
    }

    String json = "{\"active\":true,\"mode\":\"";
    json += userAppMode();
    json += "\",\"ssid\":\"";
    json += jsonEscape(user_app_using_leaf_wifi ? "Leaf WiFi" : WiFi.SSID());
    json += "\",\"ip_address\":\"";
    json += userAppAddress();
    json += "\",\"url\":\"";
    json += userAppUrl();
    json += "\",\"firmware_version\":\"";
    json += LeafVersionInfo::firmwareVersion();
    json += "\"}";
    sendNoStoreHeaders(target);
    target.send(200, "application/json", json);
  }

  String wifiNetworksJsonFromScan(int16_t scan_result) {
    if (scan_result < 0) {
      return "{\"scanning\":false,\"networks\":[]}";
    }

    static constexpr int16_t MAX_WIFI_SETUP_NETWORKS = 10;
    int16_t top_indices[MAX_WIFI_SETUP_NETWORKS];
    int32_t top_rssi[MAX_WIFI_SETUP_NETWORKS];
    int16_t top_count = 0;

    for (int16_t i = 0; i < scan_result; i++) {
      const int32_t rssi = WiFi.RSSI(i);
      int16_t insert_at = top_count;
      while (insert_at > 0 && rssi > top_rssi[insert_at - 1]) {
        insert_at--;
      }
      if (insert_at >= MAX_WIFI_SETUP_NETWORKS) continue;

      const int16_t limit = top_count < MAX_WIFI_SETUP_NETWORKS ? top_count : MAX_WIFI_SETUP_NETWORKS - 1;
      for (int16_t j = limit; j > insert_at; j--) {
        top_indices[j] = top_indices[j - 1];
        top_rssi[j] = top_rssi[j - 1];
      }
      top_indices[insert_at] = i;
      top_rssi[insert_at] = rssi;
      if (top_count < MAX_WIFI_SETUP_NETWORKS) top_count++;
    }

    String json = "{\"scanning\":false,\"networks\":[";
    for (int16_t top_i = 0; top_i < top_count; top_i++) {
      const int16_t i = top_indices[top_i];
      if (top_i > 0) json += ",";
      json += "{\"ssid\":\"";
      json += jsonEscape(WiFi.SSID(i));
      json += "\",\"rssi\":";
      json += WiFi.RSSI(i);
      json += ",\"secure\":";
      json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true";
      json += "}";
    }
    json += "]}";
    return json;
  }

  void finishWifiNetworkScan(int16_t result) {
    if (result < 0) {
      Serial.printf("Leaf WiFi setup scan failed: %d\n", result);
    }
    wifi_setup_networks_json = wifiNetworksJsonFromScan(result);
    Serial.printf("Leaf WiFi setup cycle %lu +%lums: scan=%d cachedJson=%u heap=%u maxAlloc=%u\n",
                  static_cast<unsigned long>(wifi_setup_cycle),
                  static_cast<unsigned long>(millis() - wifi_setup_started_ms),
                  result,
                  wifi_setup_networks_json.length(),
                  ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap());
    WiFi.scanDelete();
  }

  void updateWifiNetworkScan() {
    if (!wifi_setup_scan_running) return;

    const int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;

    wifi_setup_scan_running = false;
    finishWifiNetworkScan(result);
  }

  void startWifiNetworkScan() {
    logWifiSetupTiming("scan-start");
    WiFi.scanDelete();
    wifi_setup_networks_json = "{\"scanning\":true,\"networks\":[]}";
    const int16_t result = WiFi.scanNetworks(/*async=*/true, /*hidden=*/false);
    if (result == WIFI_SCAN_RUNNING) {
      wifi_setup_scan_running = true;
      return;
    }

    wifi_setup_scan_running = false;
    finishWifiNetworkScan(result);
  }

  void sendWifiNetworks(WebServer& target) {
    updateWifiNetworkScan();
    sendNoStoreHeaders(target);
    target.send(200, "application/json", wifi_setup_networks_json);
  }

  void connectToWifiFromRequest(WebServer& target) {
    const String body = target.arg("plain");
    const String ssid = extractJsonStringValue(body, "ssid");
    const String password = extractJsonStringValue(body, "password");

    if (ssid.isEmpty()) {
      target.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_ssid\"}");
      return;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.persistent(true);
    WiFi.begin(ssid.c_str(), password.c_str());
    wifi_setup_connecting = true;
    wifi_setup_connect_started_ms = millis();
    wifi_setup_connect_ssid = ssid;
    wifi_setup_connect_error = "";
    Serial.printf("Leaf WiFi setup connecting to SSID: %s\n", ssid.c_str());
    target.send(202, "application/json", "{\"ok\":true}");
  }

  void setupUserAppServer() {
    if (user_server_started) return;

    if (!user_server_routes_configured) {
      user_server.on("/", HTTP_GET, []() {
        sendRedirect(user_server, user_app_provisioning ? "/wifi" : "/app");
      });
      user_server.on("/app", HTTP_GET, []() { sendUserAppShell(user_server); });
      user_server.on("/wifi", HTTP_GET, []() { sendWifiSetupPage(user_server); });
      user_server.on("/app/wifi", HTTP_GET, []() { sendWifiSetupPage(user_server); });
      user_server.on("/api/user/status", HTTP_GET, []() { sendUserStatus(user_server); });
      user_server.on("/api/wifi/status", HTTP_GET, []() { user_server.send(200, "application/json", wifiStatusJson()); });
      user_server.on("/api/wifi/networks", HTTP_GET, []() { sendWifiNetworks(user_server); });
      user_server.on("/api/wifi/connect", HTTP_POST, []() { connectToWifiFromRequest(user_server); });
      user_server.on("/generate_204", HTTP_GET, []() { sendNoCaptivePortalResponse(user_server); });
      user_server.on("/gen_204", HTTP_GET, []() { sendNoCaptivePortalResponse(user_server); });
      user_server.on("/hotspot-detect.html", HTTP_GET,
                     []() { sendNoCaptivePortalResponse(user_server, "Success"); });
      user_server.on("/library/test/success.html", HTTP_GET,
                     []() { sendNoCaptivePortalResponse(user_server, "Success"); });
      user_server.on("/ncsi.txt", HTTP_GET,
                     []() { sendNoCaptivePortalResponse(user_server, "Microsoft NCSI"); });
      user_server.onNotFound([]() {
        if (handleCaptivePortalRequest(user_server)) return;
        user_server.send(404, "text/plain", "Not found");
      });
      user_server_routes_configured = true;
    }

    user_server.begin();
    user_server_started = true;
    Serial.printf("Leaf Web App started: %s\n", userAppUrl().c_str());
  }
}  // namespace

constexpr auto endl = "\n";

void writeScreenshotBuffer(const char* buffer) {
  Serial.println("Writing screenshot buffer");
  send_buffer += buffer;
}

void webserver_setup() {
  if (WiFi.status() != WL_CONNECTED && !user_app_enabled) return;

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

  server.on("/app", HTTP_GET, []() {
    sendUserAppShell(server);
  });

  server.on("/api/user/status", HTTP_GET, []() {
    sendUserStatus(server);
  });

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

  server.on("/discovery", HTTP_GET, []() {
    factoryDiscovery.update();
    server.send(200, "application/json", factoryDiscovery.statusJson());
  });

  server.on("/settings/factory-reset", HTTP_POST, []() {
    const bool forceFormatSdCard =
        extractJsonBoolValue(server.arg("plain"), "force_format_sd_card", false);
    settings.factoryResetVario();
    settings.setProductionTestForceFormatSdCard(forceFormatSdCard);
    server.send(200, "application/json", "{\"reset_requested\":true}");
    delay(250);
    ESP.restart();
  });

  server.on("/settings/fanet-address", HTTP_POST, []() {
    String fanet_address = extractJsonStringValue(server.arg("plain"), "fanet_address");
    fanet_address.trim();
    fanet_address.toUpperCase();
    if (!isValidFanetAddress(fanet_address)) {
      server.send(400, "application/json",
                  "{\"detail\":\"fanet_address must be a 6-character hexadecimal string.\"}");
      return;
    }

    settings.fanet_address = fanet_address;
    settings.save();

    String json = "{\"fanet_address\":\"";
    json += settings.fanet_address;
    json += "\",\"saved\":true}";
    server.send(200, "application/json", json);
  });

  server.on("/self-test/interactive", HTTP_POST, []() {
    requestInteractiveSelfTest();
    server.send(200, "application/json", selfTestSnapshotJson());
  });

  server.on("/sd-card/format", HTTP_POST, []() {
    if (selfTest.updateNeeded() || interactive_self_test_pending) {
      server.send(409, "application/json", "{\"detail\":\"Self test is running or pending.\"}");
      return;
    }

    if (!sdcard.isCardPresent()) {
      server.send(404, "application/json", "{\"detail\":\"SD card is not present.\"}");
      return;
    }

    if (!sdcard.format()) {
      server.send(500, "application/json", "{\"formatted\":false}");
      return;
    }

    if (!sdcard.setLabel()) {
      server.send(500, "application/json", "{\"formatted\":true,\"label_set\":false}");
      return;
    }

    server.send(200, "application/json", "{\"formatted\":true,\"label_set\":true}");
  });

  server.on("/self-test/details", HTTP_GET, []() { sendLatestSelfTestDetails(); });

  server.on("/self-test/results", HTTP_DELETE, []() { clearSelfTestDetailsFiles(); });

  server.on("/self-test", HTTP_GET,
            []() { server.send(200, "application/json", selfTestSnapshotJson()); });

  server.on("/commissioning/complete", HTTP_POST, []() {
    selfTest.confirmCommissioningComplete();
    server.send(200, "application/json", "{\"commissioning_complete\":true}");
  });

  // Give it a chance to settle so the startup message has a valid IP address.
  delay(250);
  // The captive portal belongs on port 80 for setting up WiFi. Keep this
  // webserver on port 81.
  server.begin(81);
  webserver_started = true;
  Serial.printf("Webserver started: http://%s:81/\n", WiFi.localIP().toString());
}

void webserver_loop() {
  if (WiFi.status() == WL_CONNECTED || user_app_enabled) {
    server.handleClient();
    if (user_app_enabled) {
      if (user_app_provisioning) updateWifiNetworkScan();
      if (user_app_provisioning) dns_server.processNextRequest();
      user_server.handleClient();
    }
    updatePendingInteractiveSelfTest();
  }
}

void webserver_enable_user_app(bool useLeafWifi) {
  if (useLeafWifi || WiFi.status() != WL_CONNECTED) {
    leaf_wifi::prepareForLeafAccessPoint();
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.softAP("Leaf WiFi");
    user_app_using_leaf_wifi = true;
  } else {
    user_app_using_leaf_wifi = false;
  }

  user_app_enabled = true;
  user_app_provisioning = false;
  setupUserAppServer();
  webserver_setup();
}

void webserver_enable_wifi_setup() {
  wifi_setup_cycle++;
  wifi_setup_started_ms = millis();
  logWifiSetupTiming("start");
  leaf_wifi::prepareForUserWifiSetupFast();
  logWifiSetupTiming("after-fast-prepare");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP("Leaf WiFi");
  logWifiSetupTiming("after-softAP");
  user_app_enabled = true;
  user_app_using_leaf_wifi = true;
  user_app_provisioning = true;
  setupUserAppServer();
  logWifiSetupTiming("after-user-server");
  dns_server.start(53, "*", WiFi.softAPIP());
  logWifiSetupTiming("after-dns");
  webserver_setup();
  logWifiSetupTiming("after-main-server");
  startWifiNetworkScan();
  logWifiSetupTiming("after-scan-start");
  Serial.printf("Leaf WiFi setup started: http://%s/wifi\n", WiFi.softAPIP().toString().c_str());
}

void webserver_disable_user_app() {
  user_app_enabled = false;
  if (user_app_provisioning) dns_server.stop();
  user_app_provisioning = false;
  if (wifi_setup_scan_running) WiFi.scanDelete();
  wifi_setup_scan_running = false;
  wifi_setup_networks_json = "{\"scanning\":false,\"networks\":[]}";
  wifi_setup_connecting = false;
  wifi_setup_connect_ssid = "";
  wifi_setup_connect_error = "";
  if (user_server_started) {
    user_server.stop();
    user_server_started = false;
  }
  if (user_app_using_leaf_wifi) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  }
  user_app_using_leaf_wifi = false;
}

bool webserver_user_app_active() { return user_app_enabled; }

String webserver_user_app_url() {
  return userAppUrl();
}
