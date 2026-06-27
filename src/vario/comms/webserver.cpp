#include "comms/webserver.h"
#include <DNSServer.h>
#include <FS.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include "comms/ble.h"
#include "comms/factory_discovery.h"
#include "comms/fanet_radio.h"
#include "comms/wifi_coordinator.h"
#include "diagnostics/heap_monitor.h"
#include "diagnostics/memory_report.h"
#include "diagnostics/self_test/selfTest.h"
#include "etl/string_stream.h"
#include "power.h"
#include "profiles/profile_store.h"
#include "storage/sd_card.h"
#include "system/version_info.h"
#include "ui/display/display.h"
#include "ui/settings/settings.h"
#include "utils/lock_guard.h"

namespace {
  ::WebServer user_server(80);
  DNSServer dns_server;
  String send_buffer = "";
  bool user_server_started = false;
  bool user_server_routes_configured = false;
  bool debug_routes_configured = false;
  bool user_app_enabled = false;
  bool user_app_using_leaf_wifi = false;
  bool user_app_provisioning = false;
  bool user_app_services_paused = false;
  bool user_app_restart_ble = false;
  bool user_app_restart_fanet = false;
  FanetRadioRegion user_app_fanet_region = FanetRadioRegion::OFF;
  String wifi_setup_networks_json = "{\"scanning\":false,\"networks\":[]}";
  bool wifi_setup_scan_running = false;
  bool wifi_setup_connecting = false;
  uint32_t wifi_setup_connect_started_ms = 0;
  String wifi_setup_connect_ssid = "";
  String wifi_setup_connect_error = "";
  uint32_t user_app_loop_count = 0;
  uint32_t user_app_handle_count = 0;
  uint32_t user_app_route_root_count = 0;
  uint32_t user_app_route_app_count = 0;
  uint32_t user_app_route_status_count = 0;
  uint32_t user_app_route_profiles_get_count = 0;
  uint32_t user_app_route_profiles_put_count = 0;
  uint32_t user_app_route_not_found_count = 0;
  uint8_t user_app_max_ap_stations = 0;

  enum class SelfTestMode { None, Interactive };

  static constexpr uint32_t SELF_TEST_POWER_ON_DELAY_MS = 10000;
  static constexpr uint32_t WIFI_SETUP_CONNECT_TIMEOUT_MS = 12000;
  static constexpr size_t PROFILE_FILE_MAX_BYTES = 4096;
  static constexpr const char* PROFILE_TEMP_FILE = "/profiles/profiles.tmp";
  static constexpr const char* PROFILE_BACKUP_FILE = "/profiles/profiles.bak";
  static constexpr const char* DIAGNOSTICS_DIR = "/diagnostics";
  static constexpr const char* USER_APP_DIAGNOSTICS_FILE = "/diagnostics/webapp_requests.csv";

  SelfTestMode last_self_test_mode = SelfTestMode::None;
  bool interactive_self_test_pending = false;
  uint32_t interactive_self_test_start_ms = 0;

  uint32_t wifi_setup_cycle = 0;
  uint32_t wifi_setup_started_ms = 0;

  void logWifiSetupTiming(const char* event) {
    Serial.printf("Leaf WiFi setup cycle %lu +%lums: %s heap=%u maxAlloc=%u\n",
                  static_cast<unsigned long>(wifi_setup_cycle),
                  static_cast<unsigned long>(millis() - wifi_setup_started_ms), event,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  void resetUserAppCounters() {
    user_app_loop_count = 0;
    user_app_handle_count = 0;
    user_app_route_root_count = 0;
    user_app_route_app_count = 0;
    user_app_route_status_count = 0;
    user_app_route_profiles_get_count = 0;
    user_app_route_profiles_put_count = 0;
    user_app_route_not_found_count = 0;
    user_app_max_ap_stations = 0;
  }

  void updateUserAppStationPeak() {
    const uint8_t station_count = WiFi.softAPgetStationNum();
    if (station_count > user_app_max_ap_stations) user_app_max_ap_stations = station_count;
  }

  void dumpUserAppCounters(const char* event) {
    if (!SD_MMC.exists(DIAGNOSTICS_DIR)) SD_MMC.mkdir(DIAGNOSTICS_DIR);

    const bool existed = SD_MMC.exists(USER_APP_DIAGNOSTICS_FILE);
    File file = SD_MMC.open(USER_APP_DIAGNOSTICS_FILE, "a", true);
    if (!file) return;

    if (!existed || file.size() == 0) {
      file.println("millis,event,enabled,using_leaf_wifi,user_server_started,debug_routes_configured,"
                   "ap_stations,max_ap_stations,wifi_status,free_heap,max_alloc_heap,loop_count,"
                   "handle_count,root,app,status,profiles_get,profiles_put,not_found,ap_ip");
    }

    updateUserAppStationPeak();
    const String ap_ip = WiFi.softAPIP().toString();
    file.printf("%lu,%s,%u,%u,%u,%u,%u,%u,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%s\n",
                static_cast<unsigned long>(millis()), event ? event : "",
                user_app_enabled ? 1 : 0, user_app_using_leaf_wifi ? 1 : 0,
                user_server_started ? 1 : 0, debug_routes_configured ? 1 : 0,
                static_cast<unsigned int>(WiFi.softAPgetStationNum()),
                static_cast<unsigned int>(user_app_max_ap_stations),
                static_cast<int>(WiFi.status()), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()),
                static_cast<unsigned long>(user_app_loop_count),
                static_cast<unsigned long>(user_app_handle_count),
                static_cast<unsigned long>(user_app_route_root_count),
                static_cast<unsigned long>(user_app_route_app_count),
                static_cast<unsigned long>(user_app_route_status_count),
                static_cast<unsigned long>(user_app_route_profiles_get_count),
                static_cast<unsigned long>(user_app_route_profiles_put_count),
                static_cast<unsigned long>(user_app_route_not_found_count), ap_ip.c_str());
    file.close();
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

  void sendLatestSelfTestDetails(WebServer& target) {
    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    String file_name = latestSelfTestDetailsFileName();
    if (file_name.isEmpty()) {
      target.send(404, "application/json", "{\"detail\":\"No self test details file found.\"}");
      return;
    }

    File file = SD_MMC.open(file_name, "r");
    if (!file) {
      target.send(500, "application/json",
                  "{\"detail\":\"Self test details file could not be opened.\"}");
      return;
    }

    target.streamFile(file, "text/plain");
    file.close();
  }

  void clearSelfTestDetailsFiles(WebServer& target) {
    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    if (selfTest.updateNeeded()) {
      target.send(409, "application/json", "{\"detail\":\"Self test is still running.\"}");
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
      target.send(500, "application/json", json);
      return;
    }

    String json = "{\"cleared\":true,\"deleted_count\":";
    json += deleted_count;
    json += "}";
    target.send(200, "application/json", json);
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

  void pauseServicesForUserApp() {
    if (user_app_services_paused) return;

    user_app_restart_ble = settings.system_bluetoothOn;
    if (user_app_restart_ble) {
      BLE::get().stop();
    }

    user_app_restart_fanet = fanetRadio.getState() == FanetRadioState::RUNNING;
    user_app_fanet_region = settings.fanet_region;
    if (user_app_restart_fanet) {
      fanetRadio.end();
    }

    user_app_services_paused = true;
  }

  void resumeServicesAfterUserApp() {
    if (!user_app_services_paused) return;

    if (user_app_restart_fanet && settings.fanet_region != FanetRadioRegion::OFF) {
      fanetRadio.begin(settings.fanet_region);
    } else if (user_app_restart_fanet && user_app_fanet_region != FanetRadioRegion::OFF) {
      fanetRadio.begin(user_app_fanet_region);
    }

    if (user_app_restart_ble && settings.system_bluetoothOn) {
      BLE::get().start();
    }

    user_app_restart_ble = false;
    user_app_restart_fanet = false;
    user_app_fanet_region = FanetRadioRegion::OFF;
    user_app_services_paused = false;
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
    json += jsonEscape(WiFi.SSID().isEmpty() && status == WL_CONNECTED ? wifi_setup_connect_ssid
                                                                       : WiFi.SSID());
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

  const char* emptyProfilesJson() {
    return "{\"schema\":\"leaf.profiles\",\"schema_version\":\"v0.1.0\","
           "\"active_pilot_id\":null,\"active_glider_id\":null,\"pilots\":[],\"gliders\":[]}";
  }

  void sendProfiles(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    if (!SD_MMC.exists(ProfileStore::filePath())) {
      sendNoStoreHeaders(target);
      target.send(200, "application/json", emptyProfilesJson());
      return;
    }

    File file = SD_MMC.open(ProfileStore::filePath(), "r");
    if (!file) {
      target.send(500, "application/json",
                  "{\"detail\":\"Profiles file could not be opened.\"}");
      return;
    }

    sendNoStoreHeaders(target);
    target.streamFile(file, "application/json");
    file.close();
  }

  bool profilesRequestLooksValid(const String& body) {
    if (body.length() == 0 || body.length() > PROFILE_FILE_MAX_BYTES) return false;
    if (extractJsonStringValue(body, "schema") != "leaf.profiles") return false;
    if (body.indexOf("\"pilots\"") < 0 || body.indexOf("\"gliders\"") < 0) return false;
    return true;
  }

  bool writeProfilesBody(const String& body) {
    if (!SD_MMC.exists(ProfileStore::directoryPath()) && !SD_MMC.mkdir(ProfileStore::directoryPath())) {
      return false;
    }

    if (SD_MMC.exists(PROFILE_TEMP_FILE)) SD_MMC.remove(PROFILE_TEMP_FILE);
    if (SD_MMC.exists(PROFILE_BACKUP_FILE)) SD_MMC.remove(PROFILE_BACKUP_FILE);

    File file = SD_MMC.open(PROFILE_TEMP_FILE, "w", true);
    if (!file) return false;

    const size_t written = file.print(body);
    file.close();
    if (written != body.length()) {
      SD_MMC.remove(PROFILE_TEMP_FILE);
      return false;
    }

    const bool hadExisting = SD_MMC.exists(ProfileStore::filePath());
    if (hadExisting && !SD_MMC.rename(ProfileStore::filePath(), PROFILE_BACKUP_FILE)) {
      SD_MMC.remove(PROFILE_TEMP_FILE);
      return false;
    }

    if (!SD_MMC.rename(PROFILE_TEMP_FILE, ProfileStore::filePath())) {
      if (hadExisting) SD_MMC.rename(PROFILE_BACKUP_FILE, ProfileStore::filePath());
      SD_MMC.remove(PROFILE_TEMP_FILE);
      return false;
    }

    if (hadExisting) SD_MMC.remove(PROFILE_BACKUP_FILE);
    return true;
  }

  void saveProfiles(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    const String body = target.arg("plain");
    if (!profilesRequestLooksValid(body)) {
      target.send(400, "application/json", "{\"detail\":\"Invalid profiles JSON.\"}");
      return;
    }

    if (!writeProfilesBody(body)) {
      target.send(500, "application/json", "{\"saved\":false}");
      return;
    }

    target.send(200, "application/json", "{\"saved\":true}");
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

    target.send(200, "text/html", R"leafapp(<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><meta http-equiv=Cache-Control content=no-store><title>Leaf</title><style>:root{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#172016;background:#f5f7f2;line-height:1.35}body{margin:0}header{background:#172016;color:white;padding:18px 20px}main{max-width:640px;margin:auto;padding:16px 18px}h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 10px}section{border-bottom:1px solid #d9dfd3;padding:18px 0}.grid{display:grid;gap:10px}.row{display:flex;gap:8px}.row>*{flex:1}.row>.small{flex:0 0 94px}label{display:block;font-size:13px;font-weight:650;margin:10px 0 4px}input,select,button{box-sizing:border-box;width:100%;font:inherit;padding:11px;border:1px solid #bac3b4;border-radius:6px;background:white}button{background:#172016;color:white;font-weight:700}.secondary{background:white;color:#172016}.danger{background:#5d1717;color:white}.muted{color:#596256}.status{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:13px;white-space:pre-wrap}.msg{min-height:20px;margin-top:10px}</style></head><body><header><h1>Leaf</h1></header><main><section><h2>Status</h2><div class=status id=status>Loading...</div></section><section><h2>Pilots</h2><label>Active pilot</label><select id=pilotList></select><label>Name</label><input id=pilotName maxlength=48 autocomplete=name><div class=row><button id=pilotSave>Save</button><button class=secondary id=pilotNew>New</button><button class="small danger" id=pilotDelete>Delete</button></div></section><section><h2>Gliders</h2><label>Active glider</label><select id=gliderList></select><div class=row><div><label>Brand</label><input id=gliderBrand maxlength=32></div><div><label>Model</label><input id=gliderModel maxlength=48></div></div><div class=row><div><label>Size</label><input id=gliderSize maxlength=16></div><div><label>Display name</label><input id=gliderDisplay maxlength=64></div></div><div class=row><button id=gliderSave>Save</button><button class=secondary id=gliderNew>New</button><button class="small danger" id=gliderDelete>Delete</button></div><p class="muted msg" id=msg></p></section></main><script>
let profiles={schema:'leaf.profiles',schema_version:'v0.1.0',active_pilot_id:null,active_glider_id:null,pilots:[],gliders:[]};
const $=id=>document.getElementById(id);
function id(){return Math.floor(Math.random()*0xffffffff).toString(16).padStart(8,'0')}
function clean(v){v=(v||'').trim();return v?v:null}
function pilotLabel(p){return p.name||'Unnamed pilot'}
function gliderLabel(g){return g.display_name||[g.brand,g.model,g.size].filter(Boolean).join(' ')||'Unnamed glider'}
function selectedPilot(){return profiles.pilots.find(p=>p.id==profiles.active_pilot_id)}
function selectedGlider(){return profiles.gliders.find(g=>g.id==profiles.active_glider_id)}
function setMsg(t){$('msg').textContent=t||''}
function render(){
  let pl=$('pilotList');pl.textContent='';
  profiles.pilots.forEach(p=>{let o=document.createElement('option');o.value=p.id;o.textContent=pilotLabel(p);pl.appendChild(o)});
  if(profiles.active_pilot_id)pl.value=profiles.active_pilot_id;
  let p=selectedPilot();$('pilotName').value=p?p.name||'':'';
  let gl=$('gliderList');gl.textContent='';
  profiles.gliders.forEach(g=>{let o=document.createElement('option');o.value=g.id;o.textContent=gliderLabel(g);gl.appendChild(o)});
  if(profiles.active_glider_id)gl.value=profiles.active_glider_id;
  let g=selectedGlider();$('gliderBrand').value=g?g.brand||'':'';$('gliderModel').value=g?g.model||'':'';$('gliderSize').value=g?g.size||'':'';$('gliderDisplay').value=g?g.display_name||'':'';
}
function normalize(){
  profiles.schema='leaf.profiles';profiles.schema_version='v0.1.0';
  profiles.pilots=(profiles.pilots||[]).filter(p=>p&&p.id&&p.name);
  profiles.gliders=(profiles.gliders||[]).filter(g=>g&&g.id&&g.model);
  if(!profiles.pilots.find(p=>p.id==profiles.active_pilot_id))profiles.active_pilot_id=profiles.pilots.length==1?profiles.pilots[0].id:null;
  if(!profiles.gliders.find(g=>g.id==profiles.active_glider_id))profiles.active_glider_id=profiles.gliders.length==1?profiles.gliders[0].id:null;
}
async function save(note){
  normalize();
  let r=await fetch('/api/profiles',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(profiles)});
  if(!r.ok)throw new Error();
  setMsg(note||'Saved.');
  render();
}
async function loadStatus(){
  try{let s=await(await fetch('/api/user/status')).json();$('status').textContent=`mode: ${s.mode}\nssid: ${s.ssid||'(none)'}\nip: ${s.ip_address||'(none)'}\nfirmware: ${s.firmware_version}`}catch(e){$('status').textContent='Unable to read status.'}
}
async function loadProfiles(){
  try{profiles=await(await fetch('/api/profiles')).json();normalize();render();setMsg('')}catch(e){setMsg('Unable to read profiles.')}
}
$('pilotList').onchange=()=>{profiles.active_pilot_id=$('pilotList').value;render();save('Active pilot saved.').catch(()=>setMsg('Unable to save.'))};
$('gliderList').onchange=()=>{profiles.active_glider_id=$('gliderList').value;render();save('Active glider saved.').catch(()=>setMsg('Unable to save.'))};
$('pilotNew').onclick=()=>{$('pilotList').value='';profiles.active_pilot_id=null;$('pilotName').value='';$('pilotName').focus();setMsg('Enter pilot name, then Save.')};
$('gliderNew').onclick=()=>{$('gliderList').value='';profiles.active_glider_id=null;['gliderBrand','gliderModel','gliderSize','gliderDisplay'].forEach(x=>$(x).value='');$('gliderModel').focus();setMsg('Enter glider details, then Save.')};
$('pilotSave').onclick=()=>{let name=clean($('pilotName').value);if(!name){setMsg('Pilot name is required.');return}let p=selectedPilot();if(!p){p={id:id(),name:''};profiles.pilots.push(p);profiles.active_pilot_id=p.id}p.name=name;save('Pilot saved.').catch(()=>setMsg('Unable to save pilot.'))};
$('gliderSave').onclick=()=>{let model=clean($('gliderModel').value);if(!model){setMsg('Glider model is required.');return}let g=selectedGlider();if(!g){g={id:id(),model:''};profiles.gliders.push(g);profiles.active_glider_id=g.id}g.brand=clean($('gliderBrand').value);g.model=model;g.size=clean($('gliderSize').value);g.display_name=clean($('gliderDisplay').value);save('Glider saved.').catch(()=>setMsg('Unable to save glider.'))};
$('pilotDelete').onclick=()=>{let p=selectedPilot();if(!p)return;profiles.pilots=profiles.pilots.filter(x=>x.id!=p.id);profiles.active_pilot_id=null;save('Pilot deleted.').catch(()=>setMsg('Unable to delete pilot.'))};
$('gliderDelete').onclick=()=>{let g=selectedGlider();if(!g)return;profiles.gliders=profiles.gliders.filter(x=>x.id!=g.id);profiles.active_glider_id=null;save('Glider deleted.').catch(()=>setMsg('Unable to delete glider.'))};
loadStatus();loadProfiles();
</script></body></html>)leafapp");
  }

  void sendWifiSetupPage(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "text/plain", "Leaf Web App is not active.");
      return;
    }

    sendNoStoreHeaders(target);
    static constexpr char WIFI_SETUP_PAGE[] =
        R"leafhtml(<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><meta http-equiv=Cache-Control content=no-store><title>Leaf WiFi</title><style>body{margin:0;background:#f5f7f2;color:#172016;font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}h1{margin:0;padding:18px 20px;background:#172016;color:white;font-size:24px}main{padding:20px;max-width:560px;margin:auto}label{display:block;font-weight:650;margin:14px 0 6px}input,select,button{box-sizing:border-box;width:100%;font:inherit;padding:12px;border:1px solid #bac3b4;border-radius:6px}button{margin-top:16px;background:#172016;color:white;font-weight:700}.status{border-bottom:1px solid #d9dfd3;color:#596256;padding-bottom:14px}.row{display:flex;gap:10px}.row input{flex:1}.show{display:flex;align-items:center;gap:6px;width:auto;white-space:nowrap}.show input{width:auto}</style><script>async function init(){let s=document.getElementById('s'),l=document.getElementById('l'),n=document.getElementById('n'),p=document.getElementById('p'),w=document.getElementById('w'),f=document.getElementById('f');l.onchange=()=>{if(l.value)n.value=l.value};w.onchange=()=>p.type=w.checked?'text':'password';async function nets(){try{let d=await(await fetch('/api/wifi/networks')).json();if(d.scanning){s.textContent='Scanning for networks...';setTimeout(nets,1500);return}l.innerHTML='<option value="">Select network...</option>';d.networks.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.textContent=x.ssid+' ('+x.rssi+' dBm)';l.appendChild(o)});s.textContent=d.networks.length?'Choose a network or type one manually.':'No networks found. Type the network name manually.'}catch(e){s.textContent='Unable to read network list. Type the network name manually.'}}async function poll(){try{let d=await(await fetch('/api/wifi/status')).json();if(d.connected){s.textContent='Connected to '+d.ssid+'. Open '+d.ip_address+':81/app';return}if(d.error){s.textContent='Unable to connect. Check password and try again.';return}s.textContent=d.target_ssid?'Trying to connect to '+d.target_ssid+'...':'Trying to connect...';setTimeout(poll,1500)}catch(e){s.textContent='Trying to connect...';setTimeout(poll,2000)}}f.onsubmit=async e=>{e.preventDefault();let ssid=n.value.trim();if(!ssid){s.textContent='Enter a network name.';return}s.textContent='Saving credentials...';try{await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:p.value})});poll()}catch(x){s.textContent='Unable to save network details. Try again.'}};nets()}</script></head><body onload=init()><h1>Leaf WiFi Setup</h1><main><p class=status id=s>Loading...</p><form id=f><label>Network</label><select id=l><option>Select network...</option></select><input id=n autocomplete=off placeholder="Type network name"><label>Password</label><div class=row><input id=p type=password autocomplete=current-password><label class=show><input id=w type=checkbox>Show</label></div><button>Save and Connect</button></form></main></body></html>)leafhtml";
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

      const int16_t limit =
          top_count < MAX_WIFI_SETUP_NETWORKS ? top_count : MAX_WIFI_SETUP_NETWORKS - 1;
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
                  static_cast<unsigned long>(millis() - wifi_setup_started_ms), result,
                  wifi_setup_networks_json.length(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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
        user_app_route_root_count++;
        sendRedirect(user_server, user_app_provisioning ? "/wifi" : "/app");
      });
      user_server.on("/app", HTTP_GET, []() {
        user_app_route_app_count++;
        sendUserAppShell(user_server);
      });
      user_server.on("/wifi", HTTP_GET, []() { sendWifiSetupPage(user_server); });
      user_server.on("/app/wifi", HTTP_GET, []() { sendWifiSetupPage(user_server); });
      user_server.on("/api/user/status", HTTP_GET, []() {
        user_app_route_status_count++;
        sendUserStatus(user_server);
      });
      user_server.on("/api/profiles", HTTP_GET, []() {
        user_app_route_profiles_get_count++;
        sendProfiles(user_server);
      });
      user_server.on("/api/profiles", HTTP_PUT, []() {
        user_app_route_profiles_put_count++;
        saveProfiles(user_server);
      });
      user_server.on("/api/wifi/status", HTTP_GET,
                     []() { user_server.send(200, "application/json", wifiStatusJson()); });
      user_server.on("/api/wifi/networks", HTTP_GET, []() { sendWifiNetworks(user_server); });
      user_server.on("/api/wifi/connect", HTTP_POST,
                     []() { connectToWifiFromRequest(user_server); });
      user_server.on("/generate_204", HTTP_GET, []() { sendNoCaptivePortalResponse(user_server); });
      user_server.on("/gen_204", HTTP_GET, []() { sendNoCaptivePortalResponse(user_server); });
      user_server.on("/hotspot-detect.html", HTTP_GET,
                     []() { sendNoCaptivePortalResponse(user_server, "Success"); });
      user_server.on("/library/test/success.html", HTTP_GET,
                     []() { sendNoCaptivePortalResponse(user_server, "Success"); });
      user_server.on("/ncsi.txt", HTTP_GET,
                     []() { sendNoCaptivePortalResponse(user_server, "Microsoft NCSI"); });
      user_server.onNotFound([]() {
        user_app_route_not_found_count++;
        if (handleCaptivePortalRequest(user_server)) return;
        user_server.send(404, "text/plain", "Not found");
      });
      user_server_routes_configured = true;
    }

    webserver_setup();
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
  if (debug_routes_configured) return;

  user_server.on("/app/debug", HTTP_GET, []() {
    user_server.send(200, "text/html", R"(
      <!DOCTYPE html>
      <html>
        <head>
          <title>Leaf Debug</title>
          <style>
            body { font-family: Arial, sans-serif; margin: 2em auto; max-width: 800px; }
          </style>
        </head>
        <body>
          <h1>Leaf Debug</h1>
          <ul>
            <li><a href="/app/debug/screenshot" target="_blank">Download Screenshot</a></li>
            <li><a href="/api/debug/mass-storage" target="_blank">Start Mass Storage</a></li>
            <li><a href="#" onclick="fetch('/api/debug/self-test/interactive', {method: 'POST'}); return false;">Start Interactive Self Test</a></li>
            <li><a href="#" onclick="fetch('/api/debug/settings/factory-reset', {method: 'POST'}); return false;">Factory Reset Settings</a></li>
            <li><a href="/api/debug/mac-address" target="_blank">MAC Address</a></li>
            <li><a href="/api/debug/firmware-version" target="_blank">Firmware Version</a></li>
            <li><a href="/app/debug/fanet" target="_blank">FANet Message Stats</a></li>
            <li><a href="/app/debug/memory" target="_blank">Memory Usage Stats</a></li>
          </ul>
        </body>
      </html>
    )");
    });

  user_server.on("/api/debug/raw-xbm", HTTP_GET, []() {
    send_buffer = "";
    u8g2_WriteBufferXBM(u8g2.getU8g2(), writeScreenshotBuffer);
    user_server.send(200, "image/x-xbitmap", send_buffer);
  });

  user_server.on("/app/debug/fanet", HTTP_GET, []() {
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

    user_server.send(200, "text/html", str.c_str());
  });

  user_server.on("/app/debug/screenshot", HTTP_GET, []() {
    user_server.send(200, "text/html", R"(
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


        fetch('/api/debug/raw-xbm')
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

  user_server.on("/api/debug/mass-storage", HTTP_GET, []() {
    // Serial.end();
    sdcard.setupMassStorage();
    user_server.send(200, "text/html", "OK!");
  });

  user_server.on("/app/debug/memory", HTTP_GET,
                 []() { user_server.send(200, "text/plain", getMemoryUsage()); });

  user_server.on("/api/debug/mac-address", HTTP_GET, []() {
    String json = "{\"mac_address\":\"";
    json += settings.getMacAddress();
    json += "\"}";
    user_server.send(200, "application/json", json);
  });

  user_server.on("/api/debug/firmware-version", HTTP_GET, []() {
    String json = "{\"firmware_version\":\"";
    json += LeafVersionInfo::firmwareVersion();
    json += "\"}";
    user_server.send(200, "application/json", json);
  });

  user_server.on("/api/debug/discovery", HTTP_GET, []() {
    factoryDiscovery.update();
    user_server.send(200, "application/json", factoryDiscovery.statusJson());
  });

  user_server.on("/api/debug/settings/factory-reset", HTTP_POST, []() {
    const bool forceFormatSdCard =
        extractJsonBoolValue(user_server.arg("plain"), "force_format_sd_card", false);
    settings.factoryResetVario();
    settings.setProductionTestForceFormatSdCard(forceFormatSdCard);
    user_server.send(200, "application/json", "{\"reset_requested\":true}");
    delay(250);
    ESP.restart();
  });

  user_server.on("/api/debug/settings/fanet-address", HTTP_POST, []() {
    String fanet_address = extractJsonStringValue(user_server.arg("plain"), "fanet_address");
    fanet_address.trim();
    fanet_address.toUpperCase();
    if (!isValidFanetAddress(fanet_address)) {
      user_server.send(400, "application/json",
                       "{\"detail\":\"fanet_address must be a 6-character hexadecimal string.\"}");
      return;
    }

    settings.fanet_address = fanet_address;
    settings.save();

    String json = "{\"fanet_address\":\"";
    json += settings.fanet_address;
    json += "\",\"saved\":true}";
    user_server.send(200, "application/json", json);
  });

  user_server.on("/api/debug/self-test/interactive", HTTP_POST, []() {
    requestInteractiveSelfTest();
    user_server.send(200, "application/json", selfTestSnapshotJson());
  });

  user_server.on("/api/debug/sd-card/format", HTTP_POST, []() {
    if (selfTest.updateNeeded() || interactive_self_test_pending) {
      user_server.send(409, "application/json", "{\"detail\":\"Self test is running or pending.\"}");
      return;
    }

    if (!sdcard.isCardPresent()) {
      user_server.send(404, "application/json", "{\"detail\":\"SD card is not present.\"}");
      return;
    }

    if (!sdcard.format()) {
      user_server.send(500, "application/json", "{\"formatted\":false}");
      return;
    }

    if (!sdcard.setLabel()) {
      user_server.send(500, "application/json", "{\"formatted\":true,\"label_set\":false}");
      return;
    }

    user_server.send(200, "application/json", "{\"formatted\":true,\"label_set\":true}");
  });

  user_server.on("/api/debug/self-test/details", HTTP_GET,
                 []() { sendLatestSelfTestDetails(user_server); });

  user_server.on("/api/debug/self-test/results", HTTP_DELETE,
                 []() { clearSelfTestDetailsFiles(user_server); });

  user_server.on("/api/debug/self-test", HTTP_GET,
                 []() { user_server.send(200, "application/json", selfTestSnapshotJson()); });

  user_server.on("/api/debug/commissioning/complete", HTTP_POST, []() {
    selfTest.confirmCommissioningComplete();
    user_server.send(200, "application/json", "{\"commissioning_complete\":true}");
  });

  debug_routes_configured = true;
}

void webserver_loop() {
  if (WiFi.status() == WL_CONNECTED || user_app_enabled) {
    if (user_app_enabled) {
      user_app_loop_count++;
      updateUserAppStationPeak();
      if (user_app_provisioning) updateWifiNetworkScan();
      if (user_app_provisioning) dns_server.processNextRequest();
      user_app_handle_count++;
      user_server.handleClient();
    }
    updatePendingInteractiveSelfTest();
  }
}

void webserver_enable_user_app(bool useLeafWifi) {
  pauseServicesForUserApp();

  heap_monitor::clear();
  resetUserAppCounters();
  heap_monitor::record("enable-start");
  if (useLeafWifi || WiFi.status() != WL_CONNECTED) {
    leaf_wifi::prepareForLeafAccessPoint();
    heap_monitor::record("after-prepare-ap");
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.softAP("Leaf WiFi");
    heap_monitor::record("after-softap");
    user_app_using_leaf_wifi = true;
  } else {
    user_app_using_leaf_wifi = false;
    heap_monitor::record("using-sta");
  }

  user_app_enabled = true;
  user_app_provisioning = false;
  setupUserAppServer();
  heap_monitor::record("after-user-server");
  webserver_setup();
  heap_monitor::record("enabled");
}

void webserver_enable_wifi_setup() {
  pauseServicesForUserApp();

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
  heap_monitor::record("disable-start");
  dumpUserAppCounters("disable-start");
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
    heap_monitor::record("after-user-stop");
  }
  if (user_app_using_leaf_wifi) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    heap_monitor::record("after-ap-stop");
  }
  user_app_using_leaf_wifi = false;
  heap_monitor::record("disabled");
  dumpUserAppCounters("disabled");
  heap_monitor::dumpToSd();
  resumeServicesAfterUserApp();
}

bool webserver_user_app_active() { return user_app_enabled; }

String webserver_user_app_url() { return userAppUrl(); }
