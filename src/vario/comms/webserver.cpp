#include "comms/webserver.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <FS.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdexcept>
#include "comms/ble.h"
#include "comms/factory_discovery.h"
#include "comms/fanet_radio.h"
#include "comms/ota.h"
#include "comms/wifi_coordinator.h"
#include "diagnostics/heap_monitor.h"
#include "diagnostics/memory_report.h"
#include "diagnostics/self_test/selfTest.h"
#include "etl/string_stream.h"
#include "logbook/logbook_store.h"
#include "navigation/gpx.h"
#include "navigation/route_store.h"
#include "navigation/user_waypoints.h"
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
  bool commissioning_routes_configured = false;
  bool debug_routes_configured = false;
  bool user_app_enabled = false;
  bool user_app_using_leaf_wifi = false;
  bool user_app_provisioning = false;
  bool user_app_services_paused = false;
  bool user_app_dns_started = false;
  bool user_app_restart_ble = false;
  bool user_app_restart_fanet = false;
  bool user_app_always_on = false;
  bool user_app_keep_bluetooth_disabled = false;
  FanetRadioRegion user_app_fanet_region = FanetRadioRegion::OFF;
  String wifi_setup_networks_json = "{\"scanning\":false,\"networks\":[]}";
  bool wifi_setup_scan_running = false;
  bool wifi_setup_connecting = false;
  uint32_t wifi_setup_connect_started_ms = 0;
  uint32_t wifi_setup_connected_ms = 0;
  String wifi_setup_connect_ssid = "";
  String wifi_setup_connect_password = "";
  String wifi_setup_connect_error = "";
  bool wifi_setup_connect_saved = false;
  int16_t wifi_setup_last_scan_result = WIFI_SCAN_FAILED;
  uint32_t user_app_loop_count = 0;
  uint32_t user_app_handle_count = 0;
  uint32_t user_app_route_root_count = 0;
  uint32_t user_app_route_app_count = 0;
  uint32_t user_app_route_status_count = 0;
  uint32_t user_app_route_profiles_get_count = 0;
  uint32_t user_app_route_profiles_put_count = 0;
  uint32_t user_app_route_logbook_count = 0;
  uint32_t user_app_route_logbook_entry_count = 0;
  uint32_t user_app_route_logbook_delete_count = 0;
  uint32_t user_app_route_waypoints_upload_count = 0;
  uint32_t user_app_route_routes_import_count = 0;
  uint32_t user_app_route_not_found_count = 0;
  uint8_t user_app_max_ap_stations = 0;

  enum class SelfTestMode { None, Interactive };

  static constexpr uint32_t SELF_TEST_POWER_ON_DELAY_MS = 10000;
  static constexpr uint32_t WIFI_SETUP_CONNECT_TIMEOUT_MS = 12000;
  static constexpr uint32_t WIFI_SETUP_AP_SUCCESS_GRACE_MS = 10000;
  static constexpr size_t PROFILE_FILE_MAX_BYTES = 4096;
  static constexpr size_t ROUTE_IMPORT_REQUEST_MAX_BYTES = 10000;
  static constexpr size_t ROUTE_EDITOR_REQUEST_MAX_BYTES = 18000;
  static constexpr size_t USER_WAYPOINTS_MAX_BYTES = 24576;
  static constexpr size_t NAV_UPLOAD_MAX_BYTES = 262144UL;
  static constexpr uint32_t NAV_POINTS_MIN_FREE_HEAP = 18000;
  static constexpr uint32_t NAV_POINTS_MIN_MAX_ALLOC = 6000;
  static constexpr const char* PROFILE_TEMP_FILE = "/profiles/profiles.tmp";
  static constexpr const char* PROFILE_BACKUP_FILE = "/profiles/profiles.bak";
  static constexpr const char* WAYPOINTS_DIR = "/waypoints";
  static constexpr const char* NAV_UPLOAD_TEMP_FILE = "/waypoints/upload.tmp";
  static constexpr const char* DIAGNOSTICS_DIR = "/diagnostics";
  static constexpr const char* USER_APP_DIAGNOSTICS_FILE = "/diagnostics/webapp_requests.csv";
  static constexpr const char* WEB_REQUEST_DIAGNOSTICS_FILE = "/diagnostics/web_requests.csv";
  static constexpr const char* WIFI_SETUP_DIAGNOSTICS_FILE = "/diagnostics/wifi_setup.csv";
  static constexpr size_t WIFI_SETUP_NETWORKS_JSON_RESERVE = 896;
  static constexpr uint32_t WEB_REQUEST_SLOW_MS = 1000;

  SelfTestMode last_self_test_mode = SelfTestMode::None;
  bool interactive_self_test_pending = false;
  uint32_t interactive_self_test_start_ms = 0;

  uint32_t wifi_setup_cycle = 0;
  uint32_t wifi_setup_started_ms = 0;
  uint32_t wifi_setup_last_diag_ms = 0;
  File nav_upload_file;
  String nav_upload_final_path = "";
  String nav_upload_saved_name = "";
  String nav_upload_error = "";
  size_t nav_upload_bytes = 0;
  uint32_t web_request_sequence = 0;

  bool diagnosticsEnabled() { return settings.dev_mode; }

  bool connectedToDiagnosticWifi() {
    return WiFi.status() == WL_CONNECTED && WiFi.SSID() == "LeafDiagnostics";
  }

  bool commissioningHttpAllowed() {
    return connectedToDiagnosticWifi() && !settings.commissioningComplete;
  }

  void logWifiSetupTiming(const char* event) {
    if (!diagnosticsEnabled()) return;
    Serial.printf("Leaf WiFi setup cycle %lu +%lums: %s heap=%u maxAlloc=%u\n",
                  static_cast<unsigned long>(wifi_setup_cycle),
                  static_cast<unsigned long>(millis() - wifi_setup_started_ms), event,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  void resetUserAppCounters() {
    if (!diagnosticsEnabled()) return;
    user_app_loop_count = 0;
    user_app_handle_count = 0;
    user_app_route_root_count = 0;
    user_app_route_app_count = 0;
    user_app_route_status_count = 0;
    user_app_route_profiles_get_count = 0;
    user_app_route_profiles_put_count = 0;
    user_app_route_logbook_count = 0;
    user_app_route_logbook_entry_count = 0;
    user_app_route_logbook_delete_count = 0;
    user_app_route_waypoints_upload_count = 0;
    user_app_route_routes_import_count = 0;
    user_app_route_not_found_count = 0;
    user_app_max_ap_stations = 0;
  }

  void updateUserAppStationPeak() {
    if (!diagnosticsEnabled()) return;
    const uint8_t station_count = WiFi.softAPgetStationNum();
    if (station_count > user_app_max_ap_stations) user_app_max_ap_stations = station_count;
  }

  bool stationConnectionReady() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  }

  uint32_t wifiSetupConnectedElapsedMs() {
    return wifi_setup_connected_ms == 0 ? 0 : millis() - wifi_setup_connected_ms;
  }

  bool wifiSetupReadyForTransition() {
    return user_app_enabled && user_app_provisioning && stationConnectionReady() &&
           wifi_setup_connected_ms != 0 &&
           wifiSetupConnectedElapsedMs() >= WIFI_SETUP_AP_SUCCESS_GRACE_MS;
  }

  void printCsvString(File& file, const String& value) {
    file.print('"');
    for (size_t i = 0; i < value.length(); i++) {
      if (value[i] == '"') file.print('"');
      file.print(value[i]);
    }
    file.print('"');
  }

  const char* httpMethodName(HTTPMethod method) {
    switch (method) {
      case HTTP_GET:
        return "GET";
      case HTTP_POST:
        return "POST";
      case HTTP_DELETE:
        return "DELETE";
      case HTTP_PUT:
        return "PUT";
      case HTTP_PATCH:
        return "PATCH";
      case HTTP_OPTIONS:
        return "OPTIONS";
      default:
        return "OTHER";
    }
  }

  void appendWebRequestDiagnostics(const char* event, uint32_t sequence, const char* route,
                                   uint32_t startedMs, uint32_t durationMs) {
    if (!diagnosticsEnabled()) return;
    if (!SD_MMC.exists(DIAGNOSTICS_DIR)) SD_MMC.mkdir(DIAGNOSTICS_DIR);

    const bool existed = SD_MMC.exists(WEB_REQUEST_DIAGNOSTICS_FILE);
    File file = SD_MMC.open(WEB_REQUEST_DIAGNOSTICS_FILE, "a", true);
    if (!file) return;

    if (!existed || file.size() == 0) {
      file.println(
          "millis,event,sequence,route,method,uri,duration_ms,started_ms,free_heap,"
          "min_free_heap,largest_free_block,max_alloc_heap,wifi_status,wifi_mode,rssi,"
          "ap_stations,using_leaf_wifi,provisioning,current_task");
    }

    file.printf("%lu,", static_cast<unsigned long>(millis()));
    printCsvString(file, event ? String(event) : String(""));
    file.printf(",%lu,", static_cast<unsigned long>(sequence));
    printCsvString(file, route ? String(route) : String(""));
    file.print(',');
    printCsvString(file, httpMethodName(user_server.method()));
    file.print(',');
    printCsvString(file, user_server.uri());
    file.printf(",%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%u,%u,%u,",
                static_cast<unsigned long>(durationMs), static_cast<unsigned long>(startedMs),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()), static_cast<int>(WiFi.status()),
                static_cast<int>(WiFi.getMode()), WiFi.RSSI(),
                static_cast<unsigned int>(WiFi.softAPgetStationNum()),
                user_app_using_leaf_wifi ? 1 : 0, user_app_provisioning ? 1 : 0);
    printCsvString(file, pcTaskGetName(NULL));
    file.println();
    file.close();
  }

  template <typename Handler>
  void handleUserRequest(const char* route, Handler handler) {
    if (!diagnosticsEnabled()) {
      handler();
      return;
    }

    const uint32_t sequence = ++web_request_sequence;
    const uint32_t startedMs = millis();
    appendWebRequestDiagnostics("start", sequence, route, startedMs, 0);
    handler();
    const uint32_t durationMs = millis() - startedMs;
    appendWebRequestDiagnostics(durationMs >= WEB_REQUEST_SLOW_MS ? "slow" : "end", sequence, route,
                                startedMs, durationMs);
  }

  void appendWifiSetupDiagnostics(const char* event, bool force = false) {
    if (!diagnosticsEnabled()) return;
    const uint32_t now = millis();
    if (!force && now - wifi_setup_last_diag_ms < 1000) return;
    wifi_setup_last_diag_ms = now;

    const bool ready = wifiSetupReadyForTransition();
    const int wifi_status = static_cast<int>(WiFi.status());
    const int wifi_mode = static_cast<int>(WiFi.getMode());
    const uint8_t ap_stations = WiFi.softAPgetStationNum();
    const String local_ip = WiFi.localIP().toString();
    const String ap_ip = WiFi.softAPIP().toString();
    const String ssid = WiFi.SSID();

    Serial.printf(
        "Leaf WiFi setup diag: event=%s enabled=%u leaf_ap=%u provisioning=%u connecting=%u "
        "ready=%u status=%d mode=%d local_ip=%s ap_ip=%s ap_stations=%u ssid=%s target=%s "
        "connected_elapsed=%lu error=%s\n",
        event ? event : "", user_app_enabled ? 1 : 0, user_app_using_leaf_wifi ? 1 : 0,
        user_app_provisioning ? 1 : 0, wifi_setup_connecting ? 1 : 0, ready ? 1 : 0, wifi_status,
        wifi_mode, local_ip.c_str(), ap_ip.c_str(), static_cast<unsigned int>(ap_stations),
        ssid.c_str(), wifi_setup_connect_ssid.c_str(),
        static_cast<unsigned long>(wifiSetupConnectedElapsedMs()),
        wifi_setup_connect_error.c_str());

    if (!SD_MMC.exists(DIAGNOSTICS_DIR)) SD_MMC.mkdir(DIAGNOSTICS_DIR);
    const bool existed = SD_MMC.exists(WIFI_SETUP_DIAGNOSTICS_FILE);
    File file = SD_MMC.open(WIFI_SETUP_DIAGNOSTICS_FILE, "a", true);
    if (!file) return;

    if (!existed || file.size() == 0) {
      file.println(
          "millis,event,cycle,enabled,using_leaf_wifi,provisioning,connecting,ready,wifi_status,"
          "wifi_mode,ap_stations,local_ip,ap_ip,ssid,target_ssid,connect_error,"
          "connect_started_ms,connected_ms,connected_elapsed_ms,last_scan_result,free_heap,"
          "max_alloc_heap");
    }

    file.printf("%lu,", static_cast<unsigned long>(now));
    printCsvString(file, event ? String(event) : String(""));
    file.printf(",%lu,%u,%u,%u,%u,%u,%d,%d,%u,", static_cast<unsigned long>(wifi_setup_cycle),
                user_app_enabled ? 1 : 0, user_app_using_leaf_wifi ? 1 : 0,
                user_app_provisioning ? 1 : 0, wifi_setup_connecting ? 1 : 0, ready ? 1 : 0,
                wifi_status, wifi_mode, static_cast<unsigned int>(ap_stations));
    printCsvString(file, local_ip);
    file.print(',');
    printCsvString(file, ap_ip);
    file.print(',');
    printCsvString(file, ssid);
    file.print(',');
    printCsvString(file, wifi_setup_connect_ssid);
    file.print(',');
    printCsvString(file, wifi_setup_connect_error);
    file.printf(",%lu,%lu,%lu,%d,%lu,%lu\n",
                static_cast<unsigned long>(wifi_setup_connect_started_ms),
                static_cast<unsigned long>(wifi_setup_connected_ms),
                static_cast<unsigned long>(wifiSetupConnectedElapsedMs()),
                static_cast<int>(wifi_setup_last_scan_result),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    file.close();
  }

  void rememberSuccessfulWifiSetupNetwork() {
    if (wifi_setup_connect_saved || wifi_setup_connect_ssid.isEmpty()) return;
    leaf_wifi::rememberSuccessfulNetwork(wifi_setup_connect_ssid, wifi_setup_connect_password);
    wifi_setup_connect_saved = true;
    wifi_setup_connect_password = "";
  }

  void markWifiSetupConnected(const char* event) {
    wifi_setup_connecting = false;
    if (wifi_setup_connected_ms == 0) wifi_setup_connected_ms = millis();
    wifi_setup_connect_error = "";
    rememberSuccessfulWifiSetupNetwork();
    appendWifiSetupDiagnostics(event, true);
  }

  void dumpUserAppCounters(const char* event) {
    if (!diagnosticsEnabled()) return;
    if (!SD_MMC.exists(DIAGNOSTICS_DIR)) SD_MMC.mkdir(DIAGNOSTICS_DIR);

    const bool existed = SD_MMC.exists(USER_APP_DIAGNOSTICS_FILE);
    File file = SD_MMC.open(USER_APP_DIAGNOSTICS_FILE, "a", true);
    if (!file) return;

    if (!existed || file.size() == 0) {
      file.println(
          "millis,event,enabled,using_leaf_wifi,user_server_started,debug_routes_configured,"
          "ap_stations,max_ap_stations,wifi_status,free_heap,max_alloc_heap,loop_count,"
          "handle_count,root,app,status,profiles_get,profiles_put,logbook,logbook_entry,"
          "logbook_delete,waypoints_upload,routes_import,not_found,ap_ip");
    }

    updateUserAppStationPeak();
    const String ap_ip = WiFi.softAPIP().toString();
    file.printf(
        "%lu,%s,%u,%u,%u,%u,%u,%u,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%"
        "s\n",
        static_cast<unsigned long>(millis()), event ? event : "", user_app_enabled ? 1 : 0,
        user_app_using_leaf_wifi ? 1 : 0, user_server_started ? 1 : 0,
        debug_routes_configured ? 1 : 0, static_cast<unsigned int>(WiFi.softAPgetStationNum()),
        static_cast<unsigned int>(user_app_max_ap_stations), static_cast<int>(WiFi.status()),
        static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(ESP.getMaxAllocHeap()),
        static_cast<unsigned long>(user_app_loop_count),
        static_cast<unsigned long>(user_app_handle_count),
        static_cast<unsigned long>(user_app_route_root_count),
        static_cast<unsigned long>(user_app_route_app_count),
        static_cast<unsigned long>(user_app_route_status_count),
        static_cast<unsigned long>(user_app_route_profiles_get_count),
        static_cast<unsigned long>(user_app_route_profiles_put_count),
        static_cast<unsigned long>(user_app_route_logbook_count),
        static_cast<unsigned long>(user_app_route_logbook_entry_count),
        static_cast<unsigned long>(user_app_route_logbook_delete_count),
        static_cast<unsigned long>(user_app_route_waypoints_upload_count),
        static_cast<unsigned long>(user_app_route_routes_import_count),
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

  class JsonStream {
   public:
    JsonStream(WebServer& target, char* buffer, size_t bufferSize)
        : target_(target), buffer_(buffer), bufferSize_(bufferSize) {}

    void write(const char* value) {
      if (value == nullptr) return;
      write(value, strlen(value));
    }

    void write(const char* value, size_t len) {
      if (value == nullptr || len == 0) return;
      if (len > bufferSize_) {
        flush();
        target_.sendContent(value, len);
        return;
      }
      if (len > bufferSize_ - used_) flush();
      memcpy(buffer_ + used_, value, len);
      used_ += len;
    }

    void writeEscaped(const char* value) {
      if (value == nullptr) return;
      const char* chunkStart = value;
      for (const char* p = value; *p != '\0'; p++) {
        const char* replacement = nullptr;
        char escapedChar[3] = {'\\', *p, '\0'};
        if (*p == '"' || *p == '\\') {
          replacement = escapedChar;
        } else if (*p == '\n') {
          replacement = "\\n";
        } else if (*p == '\r') {
          replacement = "\\r";
        }

        if (replacement != nullptr) {
          if (p > chunkStart) write(chunkStart, p - chunkStart);
          write(replacement);
          chunkStart = p + 1;
        }
      }
      if (*chunkStart != '\0') write(chunkStart, strlen(chunkStart));
    }

    void writeUInt(unsigned long value) {
      char buffer[16];
      snprintf(buffer, sizeof(buffer), "%lu", value);
      write(buffer);
    }

    void writeFloat(double value, unsigned int decimals) {
      char buffer[32];
      if (isfinite(value)) {
        snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
      } else {
        snprintf(buffer, sizeof(buffer), "null");
      }
      write(buffer);
    }

    void finish() {
      flush();
      target_.sendContent("", 0);
    }

   private:
    void flush() {
      if (used_ == 0) return;
      target_.sendContent(buffer_, used_);
      used_ = 0;
    }

    WebServer& target_;
    char* buffer_;
    const size_t bufferSize_;
    size_t used_ = 0;
  };

  String filenameFromUploadPath(String fileName) {
    fileName.replace('\\', '/');
    const int slash = fileName.lastIndexOf('/');
    if (slash >= 0) fileName = fileName.substring(slash + 1);
    fileName.trim();
    return fileName;
  }

  String lowerFileExtension(const String& fileName) {
    const int dot = fileName.lastIndexOf('.');
    if (dot < 0 || dot == fileName.length() - 1) return "";
    String ext = fileName.substring(dot + 1);
    ext.toLowerCase();
    return ext;
  }

  bool supportedNavUploadExtension(const String& ext) {
    return ext == "gpx" || ext == "cup" || ext == "wpt" || ext == "wyp";
  }

  String sanitizeNavUploadFileName(String fileName) {
    fileName = filenameFromUploadPath(fileName);
    const String ext = lowerFileExtension(fileName);
    if (!supportedNavUploadExtension(ext)) return "";

    const int dot = fileName.lastIndexOf('.');
    String stem = dot > 0 ? fileName.substring(0, dot) : "waypoints";
    stem.trim();

    String safeStem;
    safeStem.reserve(stem.length());
    for (uint16_t i = 0; i < stem.length(); i++) {
      const char c = stem[i];
      if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
        safeStem += c;
      } else if (c == ' ' || c == '.') {
        safeStem += '-';
      }
    }

    while (safeStem.startsWith("-")) safeStem.remove(0, 1);
    while (safeStem.endsWith("-")) safeStem.remove(safeStem.length() - 1);
    if (safeStem.isEmpty()) safeStem = "waypoints";

    constexpr size_t MAX_UPLOAD_FILENAME_LENGTH = maxGpxFileNameLength - 16;
    const size_t maxStemLength = MAX_UPLOAD_FILENAME_LENGTH - ext.length() - 1;
    if (safeStem.length() > maxStemLength) safeStem = safeStem.substring(0, maxStemLength);

    String safeName = safeStem;
    safeName += ".";
    safeName += ext;
    return safeName;
  }

  String uniqueWaypointUploadPath(const String& safeName) {
    const int dot = safeName.lastIndexOf('.');
    if (dot <= 0) return "";

    String stem = safeName.substring(0, dot);
    const String ext = safeName.substring(dot);
    String path = String(WAYPOINTS_DIR) + "/" + safeName;
    if (!SD_MMC.exists(path)) return path;

    for (uint16_t suffix = 2; suffix < 1000; suffix++) {
      String suffixText = "-";
      suffixText += suffix;
      constexpr size_t MAX_UPLOAD_FILENAME_LENGTH = maxGpxFileNameLength - 16;
      const size_t maxStemLength = MAX_UPLOAD_FILENAME_LENGTH - ext.length() - suffixText.length();
      String candidateStem = stem;
      if (candidateStem.length() > maxStemLength) {
        candidateStem = candidateStem.substring(0, maxStemLength);
      }
      path = String(WAYPOINTS_DIR) + "/" + candidateStem + suffixText + ext;
      if (!SD_MMC.exists(path)) return path;
    }

    return "";
  }

  bool validUserWaypointJson(JsonObjectConst point) {
    const double lat = point["lat"] | NAN;
    const double lon = point["lon"] | NAN;
    return !isnan(lat) && !isnan(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
  }

  uint8_t mergeUserWaypointIntoNavigator(JsonObjectConst point) {
    if (!validUserWaypointJson(point)) return 0;

    Waypoint waypoint;
    const char* sourceName = point["name"] | "";
    while (*sourceName == ' ' || *sourceName == '\t') sourceName++;
    char name[maxGpxNameLength + 1];
    strncpy(name, *sourceName == '\0' ? "Saved Point" : sourceName, maxGpxNameLength);
    name[maxGpxNameLength] = '\0';
    char* end = name + strlen(name);
    while (end > name && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    if (name[0] == '\0') {
      strncpy(name, "Saved Point", maxGpxNameLength);
      name[maxGpxNameLength] = '\0';
    }
    waypoint.setName(name);
    waypoint.setLatitude(point["lat"] | 0.0);
    waypoint.setLongitude(point["lon"] | 0.0);
    waypoint.ele = point["alt_m"] | 0.0;

    for (uint8_t i = 1; i <= navigator.totalWaypoints; i++) {
      if (navigator.waypoints[i].latE7 == waypoint.latE7 &&
          navigator.waypoints[i].lonE7 == waypoint.lonE7) {
        navigator.waypoints[i] = waypoint;
        return i;
      }
    }

    if (!navigator.addWaypoint(waypoint)) return 0;
    return navigator.totalWaypoints;
  }

  void resetNavUploadState() {
    if (nav_upload_file) nav_upload_file.close();
    nav_upload_final_path = "";
    nav_upload_saved_name = "";
    nav_upload_error = "";
    nav_upload_bytes = 0;
  }

  void failNavUpload(const String& message) {
    if (!nav_upload_error.isEmpty()) return;
    nav_upload_error = message;
    if (nav_upload_file) nav_upload_file.close();
    if (SD_MMC.exists(NAV_UPLOAD_TEMP_FILE)) SD_MMC.remove(NAV_UPLOAD_TEMP_FILE);
  }

  void beginNavUpload(const String& uploadFileName) {
    resetNavUploadState();

    if (!user_app_enabled) {
      nav_upload_error = "Leaf Web App is not active.";
      return;
    }

    if (!sdcard.isMounted()) {
      nav_upload_error = "SD card is not mounted.";
      return;
    }

    if (!SD_MMC.exists(WAYPOINTS_DIR) && !SD_MMC.mkdir(WAYPOINTS_DIR)) {
      nav_upload_error = "Unable to create waypoints folder.";
      return;
    }

    const String safeName = sanitizeNavUploadFileName(uploadFileName);
    if (safeName.isEmpty()) {
      nav_upload_error = "Choose a GPX, CUP, WPT, or WYP file.";
      return;
    }

    nav_upload_final_path = uniqueWaypointUploadPath(safeName);
    if (nav_upload_final_path.isEmpty()) {
      nav_upload_error = "Unable to choose a unique file name.";
      return;
    }

    if (SD_MMC.exists(NAV_UPLOAD_TEMP_FILE)) SD_MMC.remove(NAV_UPLOAD_TEMP_FILE);
    nav_upload_file = SD_MMC.open(NAV_UPLOAD_TEMP_FILE, "w", true);
    if (!nav_upload_file) {
      nav_upload_error = "Unable to write waypoint file.";
      return;
    }
  }

  void receiveWaypointUpload(WebServer& target) {
    HTTPUpload& upload = target.upload();
    switch (upload.status) {
      case UPLOAD_FILE_START:
        heap_monitor::checkpoint("waypoint-upload-start");
        beginNavUpload(upload.filename);
        break;
      case UPLOAD_FILE_WRITE:
        if (!nav_upload_error.isEmpty()) return;
        nav_upload_bytes += upload.currentSize;
        if (nav_upload_bytes > NAV_UPLOAD_MAX_BYTES) {
          failNavUpload("Waypoint file is too large.");
          return;
        }
        if (!nav_upload_file ||
            nav_upload_file.write(upload.buf, upload.currentSize) != upload.currentSize) {
          failNavUpload("Unable to write waypoint file.");
        }
        break;
      case UPLOAD_FILE_END:
        if (!nav_upload_file && nav_upload_error.isEmpty()) {
          failNavUpload("Waypoint upload did not start.");
          return;
        }
        if (nav_upload_file) nav_upload_file.close();
        if (!nav_upload_error.isEmpty()) {
          if (SD_MMC.exists(NAV_UPLOAD_TEMP_FILE)) SD_MMC.remove(NAV_UPLOAD_TEMP_FILE);
          return;
        }
        if (nav_upload_bytes == 0) {
          failNavUpload("Waypoint file is empty.");
          return;
        }
        if (SD_MMC.exists(nav_upload_final_path) ||
            !SD_MMC.rename(NAV_UPLOAD_TEMP_FILE, nav_upload_final_path)) {
          failNavUpload("Unable to save waypoint file.");
          return;
        }
        nav_upload_saved_name = filenameFromUploadPath(nav_upload_final_path);
        break;
      case UPLOAD_FILE_ABORTED:
        failNavUpload("Waypoint upload was cancelled.");
        break;
    }
  }

  void finishWaypointUpload(WebServer& target) {
    if (!nav_upload_error.isEmpty()) {
      String json = "{\"saved\":false,\"loaded\":false,\"detail\":\"";
      json += jsonEscape(nav_upload_error);
      json += "\"}";
      resetNavUploadState();
      heap_monitor::checkpoint("waypoint-upload-fail");
      target.send(400, "application/json", json);
      return;
    }

    if (nav_upload_final_path.isEmpty() || nav_upload_saved_name.isEmpty()) {
      resetNavUploadState();
      target.send(
          400, "application/json",
          "{\"saved\":false,\"loaded\":false,\"detail\":\"No waypoint file was uploaded.\"}");
      return;
    }

    const String savedPath = nav_upload_final_path;
    const String savedName = nav_upload_saved_name;
    const bool loaded = nav_readFile(SD_MMC, savedPath);
    if (loaded) user_waypoints::loadIntoNavigator();

    String json = "{\"saved\":true,\"loaded\":";
    json += loaded ? "true" : "false";
    json += ",\"filename\":\"";
    json += jsonEscape(savedName);
    json += "\",\"path\":\"";
    json += jsonEscape(savedPath);
    json += "\",\"points\":";
    json += loaded ? navigator.loadedFileWaypointCount() : 0;
    json += ",\"routes\":";
    json += loaded ? navigator.totalRoutes : 0;
    if (!loaded) {
      json += ",\"detail\":\"File was saved, but Leaf could not load it.\"";
    }
    json += "}";

    resetNavUploadState();
    heap_monitor::checkpoint(loaded ? "waypoint-upload-end" : "waypoint-upload-load-fail");
    target.send(loaded ? 200 : 422, "application/json", json);
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

  String stationUserAppUrl() {
    String url = "http://";
    url += WiFi.localIP().toString();
    url += "/app";
    return url;
  }

  String deviceHexDigits() {
    String hex = settings.getMacAddress();
    hex.replace(":", "");
    hex.toUpperCase();
    if (hex.length() < 4) return "LEAF";
    return hex;
  }

  String leafApSsid() {
    const String hex = deviceHexDigits();
    String ssid = "Leaf-";
    ssid += hex.substring(hex.length() - 4);
    return ssid;
  }

  uint8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
  }

  String leafApPassword() {
    static constexpr const char* words[] = {"sunset", "breeze", "updraft", "skyward"};
    const String hex = deviceHexDigits();
    const uint8_t a = hexNibble(hex.length() > 1 ? hex[hex.length() - 2] : '2');
    const uint8_t b = hexNibble(hex.length() > 0 ? hex[hex.length() - 1] : '3');

    String password = words[(a + b) % (sizeof(words) / sizeof(words[0]))];
    password += static_cast<char>('2' + (a % 8));
    password += static_cast<char>('2' + (b % 8));
    return password;
  }

  void startLeafAp() {
    const String ssid = leafApSsid();
    const String password = leafApPassword();
    WiFi.softAP(ssid.c_str(), password.c_str());
  }

  void startLeafApDns() {
    if (user_app_dns_started) return;
    dns_server.start(53, "*", WiFi.softAPIP());
    user_app_dns_started = true;
  }

  void stopLeafApDns() {
    if (!user_app_dns_started) return;
    dns_server.stop();
    user_app_dns_started = false;
  }

  String leafApWifiQr() {
    String qr = "WIFI:T:WPA;S:";
    qr += leafApSsid();
    qr += ";P:";
    qr += leafApPassword();
    qr += ";;";
    return qr;
  }

  void pauseServicesForUserApp() {
    if (user_app_services_paused) return;

    user_app_restart_ble = settings.system_bluetoothOn;
    if (user_app_restart_ble) {
      BLE::get().end();
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

    if (user_app_restart_ble && settings.system_bluetoothOn && !user_app_keep_bluetooth_disabled) {
      BLE::get().setup();
      BLE::get().start();
    }

    user_app_restart_ble = false;
    user_app_restart_fanet = false;
    user_app_fanet_region = FanetRadioRegion::OFF;
    user_app_services_paused = false;
  }

  void sendNoStoreHeaders(WebServer& target) {
    target.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    target.sendHeader("Pragma", "no-cache");
    target.sendHeader("Expires", "0");
  }

  void setWifiSetupNetworksJson(const char* json) {
    wifi_setup_networks_json = "";
    wifi_setup_networks_json.reserve(WIFI_SETUP_NETWORKS_JSON_RESERVE);
    wifi_setup_networks_json = json;
  }

  void releaseWifiSetupNetworksJson() {
    wifi_setup_networks_json = String();
    wifi_setup_networks_json = "{\"scanning\":false,\"networks\":[]}";
  }

  void stopFailedWifiSetupAttempt(const char* error) {
    wifi_setup_connecting = false;
    wifi_setup_connected_ms = 0;
    wifi_setup_connect_error = error;
    wifi_setup_connect_password = "";
    wifi_setup_connect_saved = false;

    // Stop the RAM-only setup attempt while keeping the previous saved default
    // and the Leaf AP available.
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    startLeafAp();
  }

  String wifiStatusJson() {
    appendWifiSetupDiagnostics("status", true);
    wl_status_t status = WiFi.status();
    if (wifi_setup_connecting && stationConnectionReady()) {
      markWifiSetupConnected("status-connected");
    } else if (wifi_setup_connecting &&
               millis() - wifi_setup_connect_started_ms > WIFI_SETUP_CONNECT_TIMEOUT_MS) {
      const wl_status_t timed_out_status = WiFi.status();
      if (timed_out_status == WL_CONNECTED) {
        markWifiSetupConnected("status-connected-after-timeout");
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
    json += ",\"transition_ready\":";
    json += wifiSetupReadyForTransition() ? "true" : "false";
    json += ",\"using_leaf_wifi\":";
    json += user_app_using_leaf_wifi ? "true" : "false";
    json += ",\"wifi_mode\":";
    json += static_cast<int>(WiFi.getMode());
    json += ",\"ap_stations\":";
    json += static_cast<int>(WiFi.softAPgetStationNum());
    json += ",\"connected_elapsed_ms\":";
    json += static_cast<unsigned long>(wifiSetupConnectedElapsedMs());
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
    json += status == WL_CONNECTED ? stationUserAppUrl() : userAppUrl();
    json += "\"}";
    return json;
  }

  const char* emptyProfilesJson() {
    return "{\"schema\":\"leaf.profiles\",\"schema_version\":\"v0.1.0\","
           "\"active_pilot_id\":null,\"active_glider_id\":null,\"pilots\":[],\"gliders\":[]}";
  }

  void sendProfiles(WebServer& target) {
    heap_monitor::checkpoint("profiles-get-start");
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
      target.send(500, "application/json", "{\"detail\":\"Profiles file could not be opened.\"}");
      return;
    }

    sendNoStoreHeaders(target);
    target.streamFile(file, "application/json");
    file.close();
    heap_monitor::checkpoint("profiles-get-end");
  }

  bool profilesRequestLooksValid(const String& body) {
    if (body.length() == 0 || body.length() > PROFILE_FILE_MAX_BYTES) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) return false;

    const char* schema = doc["schema"] | "";
    if (strcmp(schema, "leaf.profiles") != 0) return false;

    JsonArray pilots = doc["pilots"].as<JsonArray>();
    JsonArray gliders = doc["gliders"].as<JsonArray>();
    if (pilots.isNull() || gliders.isNull()) return false;
    if (pilots.size() > 12 || gliders.size() > 12) return false;

    String activePilotId =
        doc["active_pilot_id"].isNull() ? "" : doc["active_pilot_id"].as<String>();
    String activeGliderId =
        doc["active_glider_id"].isNull() ? "" : doc["active_glider_id"].as<String>();
    bool activePilotFound = activePilotId.isEmpty();
    bool activeGliderFound = activeGliderId.isEmpty();

    for (JsonObject item : pilots) {
      String id = item["id"] | "";
      String name = item["name"] | "";
      id.trim();
      name.trim();
      String email = item["email"] | "";
      String leafLogApiKey = item["leaf_log_api_key"] | "";
      email.trim();
      leafLogApiKey.trim();
      if (id.isEmpty() || id.length() > 20 || name.isEmpty() || name.length() > 48) return false;
      if (email.length() > 80) return false;
      if (leafLogApiKey.length() > 160) return false;
      if (!activePilotId.isEmpty() && id == activePilotId) activePilotFound = true;
    }

    for (JsonObject item : gliders) {
      String id = item["id"] | "";
      String brand = item["brand"] | "";
      String model = item["model"] | "";
      String size = item["size"] | "";
      String displayName = item["display_name"] | "";
      id.trim();
      brand.trim();
      model.trim();
      size.trim();
      displayName.trim();
      if (id.isEmpty() || id.length() > 20 || model.isEmpty() || model.length() > 48) return false;
      if (brand.length() > 32 || size.length() > 16 || displayName.length() > 64) return false;
      if (!activeGliderId.isEmpty() && id == activeGliderId) activeGliderFound = true;
    }

    if (!activePilotFound || !activeGliderFound) return false;
    return true;
  }

  bool writeProfilesBody(const String& body) {
    if (!SD_MMC.exists(ProfileStore::directoryPath()) &&
        !SD_MMC.mkdir(ProfileStore::directoryPath())) {
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
    heap_monitor::checkpoint("profiles-put-start");
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
      heap_monitor::checkpoint("profiles-put-invalid");
      target.send(400, "application/json", "{\"detail\":\"Invalid profiles JSON.\"}");
      return;
    }

    if (!writeProfilesBody(body)) {
      heap_monitor::checkpoint("profiles-put-fail");
      target.send(500, "application/json", "{\"saved\":false}");
      return;
    }

    heap_monitor::checkpoint("profiles-put-end");
    target.send(200, "application/json", "{\"saved\":true}");
  }

  void importRoute(WebServer& target) {
    heap_monitor::checkpoint("route-import-start");
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    const String body = target.arg("plain");
    if (body.length() == 0 || body.length() > ROUTE_IMPORT_REQUEST_MAX_BYTES) {
      target.send(400, "application/json", "{\"detail\":\"Route import request is invalid.\"}");
      return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
      target.send(400, "application/json", "{\"detail\":\"Route import request is not JSON.\"}");
      return;
    }

    String name = doc["name"] | "";
    String data = doc["data"] | "";
    const bool activate = doc["activate"] | false;
    name.trim();
    data.trim();
    if (name.isEmpty() || data.isEmpty()) {
      target.send(400, "application/json", "{\"detail\":\"Route name and data are required.\"}");
      return;
    }
    doc.clear();

    route_store::ImportResult result;
    if (!route_store::importRouteText(name, data, activate, result)) {
      String json = "{\"saved\":";
      json += result.path.isEmpty() ? "false" : "true";
      json += ",\"detail\":\"";
      json += jsonEscape(result.error);
      json += "\"}";
      heap_monitor::checkpoint("route-import-fail");
      target.send(result.path.isEmpty() ? 400 : 500, "application/json", json);
      return;
    }

    String json = "{\"saved\":true,\"active\":";
    json += result.active ? "true" : "false";
    json += ",\"name\":\"";
    json += jsonEscape(name);
    json += "\",\"path\":\"";
    json += jsonEscape(result.path);
    json += "\",\"points\":";
    json += result.points;
    json += "}";
    heap_monitor::checkpoint("route-import-end");
    target.send(200, "application/json", json);
  }

  void sendNavData(WebServer& target) {
    heap_monitor::checkpoint("nav-data-start");
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    const bool includePoints = target.hasArg("points") && target.arg("points") == "1";
    target.setContentLength(CONTENT_LENGTH_UNKNOWN);
    const bool lowHeap = includePoints && (ESP.getFreeHeap() < NAV_POINTS_MIN_FREE_HEAP ||
                                           ESP.getMaxAllocHeap() < NAV_POINTS_MIN_MAX_ALLOC);
    target.send(lowHeap ? 503 : 200, "application/json", "");
    char responseBuffer[1024];
    JsonStream json(target, responseBuffer, sizeof(responseBuffer));
    json.write("{\"loaded_file\":\"");
    json.writeEscaped(navigator.loadedNavFilename());
    json.write("\",\"loaded_path\":\"");
    json.writeEscaped(navigator.loadedNavPath());
    json.write("\",\"point_count\":");
    json.writeUInt(navigator.loadedFileWaypointCount());
    json.write(",\"route_count\":");
    json.writeUInt(navigator.totalRoutes);
    json.write(",\"active_type\":\"");
    if (navigator.activeRouteIndex) {
      json.write("route");
    } else if (navigator.activeWaypointIndex) {
      json.write("point");
    }
    json.write("\",\"active_name\":\"");
    if (navigator.activeRouteIndex && navigator.activeRouteIndex <= navigator.totalRoutes) {
      json.writeEscaped(navigator.routes[navigator.activeRouteIndex].name);
    } else if (navigator.activeWaypointIndex) {
      json.writeEscaped(navigator.activePoint.name);
    }
    json.write("\"");
    if (!includePoints) {
      json.write("}");
      json.finish();
      heap_monitor::checkpoint("nav-data-end");
      return;
    }

    if (lowHeap) {
      json.write(",\"points_unavailable\":true,\"detail\":\"Unable to refresh waypoint list.\"}");
      json.finish();
      heap_monitor::checkpoint("nav-data-low-heap");
      return;
    }

    json.write(",\"points\":[");
    const uint8_t pointCount = navigator.loadedFileWaypointCount();
    for (uint8_t i = 1; i <= pointCount; i++) {
      const Waypoint& waypoint = navigator.waypoint(WaypointID(i));
      if (i > 1) json.write(",");
      json.write("{\"index\":");
      json.writeUInt(i);
      json.write(",\"name\":\"");
      json.writeEscaped(waypoint.name);
      json.write("\",\"lat\":");
      json.writeFloat(waypoint.latitude(), 7);
      json.write(",\"lon\":");
      json.writeFloat(waypoint.longitude(), 7);
      json.write(",\"alt_m\":");
      json.writeFloat(waypoint.ele, 1);
      json.write("}");
      if ((i & 0x07) == 0) yield();
    }
    json.write("]}");
    json.finish();
    heap_monitor::checkpoint("nav-data-end");
  }

  void activateNavPoint(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, target.arg("plain"));
    if (error) {
      target.send(400, "application/json", "{\"detail\":\"Nav activation request is not JSON.\"}");
      return;
    }

    const uint8_t index = doc["index"] | 0;
    if (index == 0 || index > navigator.totalWaypoints) {
      target.send(400, "application/json", "{\"detail\":\"Choose a valid waypoint.\"}");
      return;
    }

    if (!navigator.activatePoint(WaypointID(index), true)) {
      target.send(400, "application/json", "{\"detail\":\"Leaf could not activate this point.\"}");
      return;
    }

    String json = "{\"active\":true,\"index\":";
    json += index;
    json += ",\"name\":\"";
    json += jsonEscape(navigator.activePoint.name);
    json += "\"}";
    target.send(200, "application/json", json);
  }

  void sendWaypointFileList(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    target.setContentLength(CONTENT_LENGTH_UNKNOWN);
    target.send(200, "application/json", "");
    char responseBuffer[1024];
    JsonStream json(target, responseBuffer, sizeof(responseBuffer));
    json.write("{\"files\":[");
    bool first = true;
    File dir = SD_MMC.open(WAYPOINTS_DIR);
    if (dir && dir.isDirectory()) {
      File entry = dir.openNextFile();
      while (entry) {
        if (!entry.isDirectory()) {
          const String name = filenameFromUploadPath(entry.name());
          if (supportedNavUploadExtension(lowerFileExtension(name))) {
            if (!first) json.write(",");
            first = false;
            json.write("{\"name\":\"");
            json.writeEscaped(name.c_str());
            json.write("\",\"path\":\"");
            json.writeEscaped(WAYPOINTS_DIR);
            json.write("/");
            json.writeEscaped(name.c_str());
            json.write("\"}");
          }
        }
        entry.close();
        entry = dir.openNextFile();
      }
    }
    if (dir) dir.close();
    json.write("]}");
    json.finish();
  }

  void activateWaypointFile(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, target.arg("plain"));
    if (error) {
      target.send(400, "application/json",
                  "{\"detail\":\"Waypoint activation request is invalid.\"}");
      return;
    }

    const String name = filenameFromUploadPath(doc["name"] | "");
    if (name.isEmpty() || !supportedNavUploadExtension(lowerFileExtension(name))) {
      target.send(400, "application/json", "{\"detail\":\"Choose a waypoint file.\"}");
      return;
    }

    const String path = String(WAYPOINTS_DIR) + "/" + name;
    if (!SD_MMC.exists(path)) {
      target.send(404, "application/json", "{\"detail\":\"Waypoint file was not found.\"}");
      return;
    }

    if (!nav_readFile(SD_MMC, path)) {
      target.send(400, "application/json",
                  "{\"detail\":\"Leaf could not parse this waypoint file.\"}");
      return;
    }
    user_waypoints::loadIntoNavigator();

    String json = "{\"loaded\":true,\"filename\":\"";
    json += jsonEscape(name);
    json += "\",\"points\":";
    json += navigator.loadedFileWaypointCount();
    json += ",\"routes\":";
    json += navigator.totalRoutes;
    json += "}";
    target.send(200, "application/json", json);
  }

  void saveEditedRoute(WebServer& target) {
    heap_monitor::checkpoint("route-save-start");
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    if (!sdcard.isMounted()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    const String body = target.arg("plain");
    if (body.length() == 0 || body.length() > ROUTE_EDITOR_REQUEST_MAX_BYTES) {
      target.send(400, "application/json", "{\"detail\":\"Route save request is invalid.\"}");
      return;
    }

    JsonDocument input;
    DeserializationError error = deserializeJson(input, body);
    if (error) {
      target.send(400, "application/json", "{\"detail\":\"Route save request is not JSON.\"}");
      return;
    }

    String name = input["name"] | "";
    name.trim();
    const bool activate = input["activate"] | true;
    JsonArrayConst inputPoints = input["points"].as<JsonArrayConst>();
    if (name.isEmpty() || inputPoints.isNull() || inputPoints.size() == 0 ||
        inputPoints.size() > maxRoutePointRefs) {
      target.send(400, "application/json",
                  "{\"detail\":\"Route name and at least one waypoint are required.\"}");
      return;
    }

    JsonDocument routeDoc;
    JsonObject route = routeDoc.to<JsonObject>();
    route["schema"] = "leaf.route";
    route["schema_version"] = "v0.1.0";
    route["name"] = name;
    route["source_format"] = "leaf_web";
    route["task_type"] = "route";
    route["earth_model"] = "wgs84";
    route["goal"]["type"] = "cylinder";
    route["start"]["type"] = "none";
    route["start"]["time_gates"].to<JsonArray>();

    JsonArray points = route["points"].to<JsonArray>();
    uint8_t pointIndex = 0;
    const uint8_t totalPoints = inputPoints.size();
    for (JsonObjectConst inputPoint : inputPoints) {
      pointIndex++;
      const double lat = inputPoint["lat"] | NAN;
      const double lon = inputPoint["lon"] | NAN;
      if (isnan(lat) || isnan(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        target.send(400, "application/json",
                    "{\"detail\":\"Route contains an invalid waypoint coordinate.\"}");
        return;
      }

      uint16_t radiusM = inputPoint["radius_m"] | defaultWaypointRadius;
      if (radiusM < 10) radiusM = 10;
      if (radiusM > 20000) radiusM = 20000;

      JsonObject outPoint = points.add<JsonObject>();
      outPoint["name"] = inputPoint["name"] | "";
      outPoint["lat"] = lat;
      outPoint["lon"] = lon;
      outPoint["alt_m"] = inputPoint["alt_m"] | 0.0;
      outPoint["radius_m"] = radiusM;
      outPoint["role"] = pointIndex == totalPoints ? "goal" : "normal";
    }
    input.clear();

    route_store::ImportResult result;
    if (!route_store::saveRouteJson(routeDoc, activate, result)) {
      String json = "{\"saved\":";
      json += result.path.isEmpty() ? "false" : "true";
      json += ",\"detail\":\"";
      json += jsonEscape(result.error);
      json += "\"}";
      heap_monitor::checkpoint("route-save-fail");
      target.send(result.path.isEmpty() ? 400 : 500, "application/json", json);
      return;
    }
    if (result.active) user_waypoints::loadIntoNavigator();

    String json = "{\"saved\":true,\"active\":";
    json += result.active ? "true" : "false";
    json += ",\"name\":\"";
    json += jsonEscape(name);
    json += "\",\"path\":\"";
    json += jsonEscape(result.path);
    json += "\",\"points\":";
    json += result.points;
    json += "}";
    heap_monitor::checkpoint("route-save-end");
    target.send(200, "application/json", json);
  }

  void sendUserWaypoints(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    if (!sdcard.isMounted()) {
      target.send(404, "application/json",
                  "{\"points\":[],\"detail\":\"SD card is not mounted.\"}");
      return;
    }

    JsonDocument doc;
    if (!SD_MMC.exists(user_waypoints::filePath())) {
      doc["schema"] = "leaf.user_waypoints";
      doc["schema_version"] = "v0.1.0";
      doc["points"].to<JsonArray>();
    } else {
      File file = SD_MMC.open(user_waypoints::filePath(), "r");
      if (!file) {
        target.send(404, "application/json",
                    "{\"points\":[],\"detail\":\"Unable to read user waypoints.\"}");
        return;
      }
      if (file.size() > USER_WAYPOINTS_MAX_BYTES) {
        file.close();
        target.send(404, "application/json",
                    "{\"points\":[],\"detail\":\"User waypoint file is too large.\"}");
        return;
      }
      DeserializationError error = deserializeJson(doc, file);
      file.close();
      if (error || strcmp(doc["schema"] | "", "leaf.user_waypoints") != 0) {
        target.send(404, "application/json",
                    "{\"points\":[],\"detail\":\"User waypoint file is invalid.\"}");
        return;
      }
    }

    target.setContentLength(CONTENT_LENGTH_UNKNOWN);
    target.send(200, "application/json", "");
    char responseBuffer[1024];
    JsonStream json(target, responseBuffer, sizeof(responseBuffer));
    json.write("{\"points\":[");
    bool first = true;
    for (JsonObjectConst point : doc["points"].as<JsonArrayConst>()) {
      const double lat = point["lat"] | NAN;
      const double lon = point["lon"] | NAN;
      if (!validUserWaypointJson(point)) continue;
      const uint8_t navigatorIndex = mergeUserWaypointIntoNavigator(point);

      if (!first) json.write(",");
      first = false;
      json.write("{\"id\":\"");
      json.writeEscaped(point["id"] | "");
      json.write("\",\"index\":");
      json.writeUInt(navigatorIndex);
      json.write(",\"name\":\"");
      json.writeEscaped(point["name"] | "");
      json.write("\",\"lat\":");
      json.writeFloat(lat, 7);
      json.write(",\"lon\":");
      json.writeFloat(lon, 7);
      json.write(",\"alt_m\":");
      json.writeFloat(point["alt_m"] | 0.0, 1);
      json.write(",\"created_utc\":\"");
      json.writeEscaped(point["created_utc"] | "");
      json.write("\"}");
    }
    json.write("]}");
    json.finish();
  }

  void saveUserWaypoints(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    JsonDocument input;
    DeserializationError error = deserializeJson(input, target.arg("plain"));
    if (error) {
      target.send(400, "application/json", "{\"detail\":\"User waypoint request is not JSON.\"}");
      return;
    }

    String detail;
    if (!user_waypoints::renameFromJson(input, detail)) {
      String json = "{\"saved\":false,\"detail\":\"";
      json += jsonEscape(detail);
      json += "\"}";
      target.send(400, "application/json", json);
      return;
    }

    user_waypoints::loadIntoNavigator();
    target.send(200, "application/json", "{\"saved\":true}");
  }

  void deleteUserWaypoint(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    String id = target.arg("id");
    id.trim();
    String detail;
    if (!user_waypoints::deleteById(id.c_str(), detail)) {
      String json = "{\"deleted\":false,\"detail\":\"";
      json += jsonEscape(detail);
      json += "\"}";
      target.send(400, "application/json", json);
      return;
    }

    target.send(200, "application/json", "{\"deleted\":true}");
  }

  void sendRedirect(WebServer& target, const char* location) {
    sendNoStoreHeaders(target);
    target.sendHeader("Location", location, true);
    target.send(302, "text/plain", "");
  }

  bool handleCaptivePortalRequest(WebServer& target) {
    if (!user_app_using_leaf_wifi) return false;
    sendRedirect(target, user_app_provisioning ? "/wifi" : "/app");
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

    static constexpr char USER_APP_PAGE[] PROGMEM =
        R"leafapp(<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><meta http-equiv=Cache-Control content=no-store><title>Leaf</title><style>:root{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#202423;background:#363636;line-height:1.35;--leaf:#d8ff00;--ink:#202423;--panel:#565656;--sub:#4d4d4d;--danger:#7a1d1d}body{margin:0;background:#363636}header{background:var(--leaf);color:#0b0d0b;padding:11px 20px;text-align:center}main{max-width:640px;margin:auto;padding:18px}h1{font-family:Arial,sans-serif;font-size:38px;font-weight:500;letter-spacing:.12em;line-height:1;margin:0}h2{position:relative;font-size:18px;margin:-16px -14px 14px;padding:12px 12px;color:#0b0d0b;background:var(--leaf);text-align:center;border-radius:5px 5px 0 0}section{background:var(--panel);border-radius:8px;margin:0 0 14px;padding:16px 14px}.status-panel{padding-top:14px}.status-panel h2{background:var(--panel);color:white;border-bottom:1px solid #a9a9a9;margin:-14px -14px 14px}.view{display:none}.view.active{display:block}.subbar{position:relative;display:flex;align-items:center;justify-content:center;min-height:34px;color:white;margin:0 0 14px}.back{position:absolute;left:0;top:-3px;width:44px;height:40px;background:white;color:var(--ink);border-color:white;box-shadow:none;padding:3px 8px;font-size:32px;font-weight:900;line-height:.85}.subbar h2{color:white;background:transparent;margin:0;padding:0;font-size:18px}.row{display:flex;gap:8px}.row>*{flex:1}.row>.small{flex:0 0 94px}.actions{display:flex;align-items:center;gap:10px;margin-top:10px}.actions .msg{flex:1;margin:0}.actions button,.profile-actions button{width:auto;padding:8px 10px;font-size:14px}.profile-actions{margin-top:12px;gap:14px}label{display:block;font-size:13px;font-weight:700;margin:10px 0 4px;color:white}input,select,textarea,button{box-sizing:border-box;width:100%;font:inherit;padding:11px;border:1px solid #b9c0b2;border-radius:7px;background:white;color:var(--ink)}textarea{min-height:112px;resize:vertical}.checkline{display:flex;align-items:center;gap:8px;width:auto;margin:0}.checkline input{width:auto;accent-color:var(--leaf)}.route-actions{align-items:center;justify-content:space-between}.route-actions .checkline{flex:1}.route-actions button{flex:0 0 auto}.route-edit-list{margin-top:4px;background:var(--sub);border-radius:7px;padding:8px 10px;min-height:34px}.route-point-row{display:grid;grid-template-columns:minmax(0,1fr) 74px 34px 34px 34px;gap:6px;align-items:center;margin:7px 0}.route-point-name{color:white;font-weight:750;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.route-point-row input{padding:7px}.route-point-row button{width:34px;height:34px;padding:0}.user-waypoint-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center;margin:7px 0}.user-waypoint-row input{padding:8px}.user-waypoint-meta{color:#e2e7dc;font-size:12px;white-space:nowrap}.route-add-grid{display:grid;grid-template-columns:minmax(0,1fr) 122px;gap:8px;align-items:end}.route-add-grid label{margin-top:10px}.card-nav{position:absolute;right:8px;top:5px;display:flex;align-items:center;justify-content:center;width:44px;height:38px;padding:0 0 4px;background:white;color:var(--ink);border:1px solid #0b0d0b;box-shadow:none;font-size:34px;line-height:1}.waypoint-status-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:start}.waypoint-status-row button{width:auto;padding:8px 10px;font-size:14px}.file-activate{display:block;margin-top:8px}.nav-tools-blocked{color:white;font-weight:750;margin:8px 0}.nav-summary{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:13px;white-space:pre-wrap;color:white}input:focus,select:focus,textarea:focus,button:focus{outline:2px solid var(--leaf);outline-offset:1px}select:disabled,button:disabled,.secondary:disabled,.danger:disabled{background:#686868;border-color:#686868;color:#8a8a8a;opacity:1;box-shadow:none}button{background:var(--ink);color:white;font-weight:750;border-color:var(--ink);box-shadow:inset 0 -2px 0 rgba(0,0,0,.22)}.secondary{background:white;color:var(--ink);border-color:#89917f;box-shadow:none}.danger{background:var(--danger);border-color:var(--danger);color:white}.hero{background:var(--leaf);border-color:var(--leaf);color:#0b0d0b}.profile-actions button:first-child:not(:disabled){background:var(--leaf);border-color:var(--leaf);color:#0b0d0b}.muted{color:#e2e7dc}.msg{min-height:0;margin:6px 0 0;line-height:1.2}.msg:empty{display:none}#mainView section{padding-bottom:10px}#profilesView section{padding-bottom:4px}#profilesView .msg{min-height:0;margin:6px 0 0;line-height:1.2}.leaf-log-panel{display:none;margin-top:10px;background:rgba(0,0,0,.16);border-radius:7px;padding:10px}.leaf-log-panel.active{display:block}.leaf-log-step{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;color:white;font-weight:700}.leaf-log-step button,#leafLogWifi{width:auto}.status{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:13px;white-space:pre-wrap;color:white;min-width:0}.status-body{display:grid;grid-template-columns:minmax(0,1fr) max-content;align-items:start;justify-content:space-between;column-gap:14px}.status-side{display:grid;gap:8px;justify-items:end}.firmware-check{margin-top:8px;justify-content:flex-end}.firmware-check button{width:auto;padding:7px 9px;font-size:13px}.battery-status{color:white;text-align:right;font-size:13px;font-weight:700}.battery-line{display:flex;align-items:center;justify-content:flex-end;gap:7px;margin-bottom:4px}.battery{position:relative;width:44px;height:20px;border:2px solid white;border-radius:4px;box-sizing:border-box}.battery:after{content:"";position:absolute;right:-6px;top:4px;width:4px;height:8px;background:white;border-radius:0 2px 2px 0}.battery-fill{display:block;height:100%;background:var(--leaf);border-radius:2px}.battery-meta{font-size:12px;font-weight:650;color:#e2e7dc}.metrics{display:grid;grid-template-columns:1fr 1fr;gap:9px}.metric{background:var(--sub);border-radius:7px;padding:8px 10px;color:white}.metric span{display:block;color:#dfe5d9;font-size:12px;font-weight:650;margin-bottom:2px}.metric strong{display:block;font-size:16px}#logCount{font-size:34px;line-height:1}.pager{display:grid;grid-template-columns:44px 1fr 44px;align-items:center;gap:8px;margin-bottom:12px}.pager button{height:38px;padding:0;background:var(--leaf);border-color:var(--leaf);color:#0b0d0b}.pager button:disabled{background:#686868;border-color:#686868;color:#8a8a8a;box-shadow:none}.page-title{text-align:center;color:white;font-weight:800}.flight-head{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;color:white;margin-bottom:8px}.flight-head div:nth-child(2){text-align:center}.flight-head div:nth-child(3){text-align:right}.flight-head span{display:block;color:#dfe5d9;font-size:12px;font-weight:650}.flight-head strong{display:block;color:white;font-size:14px;font-weight:800;white-space:nowrap}.flight-profiles{display:flex;justify-content:space-between;gap:10px;color:var(--leaf);font-weight:800;margin:0 0 10px}.flight-profiles div{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.flight-profiles div:last-child{text-align:right}.flight-card{color:white}.alt-box,.vario-box{background:var(--sub);border-radius:7px;padding:12px;margin:10px 0}.alt-box{position:relative;padding:8px 10px 28px}.alt-title{position:absolute;left:0;right:0;bottom:6px;text-align:center}.alt-title,.vario-title{font-size:18px;font-weight:800}.vario-title{text-align:center}.alt-row{position:relative;height:100px;margin-top:0}.alt-row .pill{position:absolute;min-width:74px;background:#111;color:white}.alt-row .pill.high{background:var(--leaf);color:#0b0d0b}.pill{background:var(--leaf);color:#0b0d0b;border-radius:6px;padding:5px 8px;font-weight:800;text-align:center}.pill span{display:block;font-size:11px}.detail-grid{display:grid;grid-template-columns:1fr 1.45fr;gap:10px}.vario-box{display:flex;flex-direction:column;justify-content:center;gap:9px}.vario-title{order:2}.vario-values{display:contents}#climbMax{order:1}#sinkMax{order:3}.sink{background:#111;color:white}.mini-metrics{background:var(--sub);border-radius:7px;padding:8px 10px}.mini-row{display:flex;justify-content:space-between;gap:8px;border-bottom:1px solid #777;padding:5px 0}.mini-row:last-child{border-bottom:0}.track{overflow-wrap:anywhere;color:#e2e7dc;margin-top:12px;font-size:13px;display:flex;justify-content:space-between;gap:12px;align-items:baseline}.track-file-name{color:var(--leaf);font-weight:800}.flight-id{font-size:12px;text-align:right;white-space:nowrap}.delete-area{margin-top:10px;display:flex;justify-content:flex-end}.delete-area>button{width:auto;padding:8px 10px;font-size:14px}.delete-confirm{display:none;width:100%;text-align:left;background:rgba(0,0,0,.18);border-radius:7px;padding:7px 8px}.delete-confirm button{width:100%;font-size:15px;padding:8px 9px}.delete-warning{font-weight:500;margin:0 0 7px;color:white}#logDetailMsg{min-height:0;margin:6px 0 0}#logDetailMsg:empty{display:none}@media(max-width:520px){.detail-grid{grid-template-columns:1fr 1.45fr}.status-body{grid-template-columns:minmax(0,1fr) max-content}.status-side{justify-items:end}.battery-status{text-align:right}.battery-line{justify-content:flex-end}}</style></head><body><header><h1>Leaf</h1></header><main><div id=mainView class="view active"><section class=status-panel><h2>Status</h2><div class=status-body><div class=status id=status>Loading...</div><div class=status-side><div class=battery-status id=batteryBox><div class=battery-line><span id=batteryText>--%</span><div class=battery><span class=battery-fill id=batteryFill></span></div></div><div class=battery-meta id=batteryCharge>Unknown</div></div><div class="actions firmware-check" id=firmwareCheckRow><button class=secondary id=firmwareCheck>Check for Updates</button></div></div></div><p class="muted msg" id=firmwareCheckMsg></p></section><section><h2>Profiles<button class=card-nav id=openProfiles aria-label="Edit Profiles">&#x276f;</button></h2><label>Active pilot</label><select id=activePilotList></select><label>Active glider</label><select id=activeGliderList></select><p class="muted msg" id=mainProfileMsg></p></section><section><h2>Logbook<button class=card-nav id=openLogbook aria-label="Open Logbook">&#x276f;</button></h2><div class=metrics><div class=metric><span>Total Flights</span><strong id=logCount>--</strong></div><div class=metric><span>Last Flight</span><strong id=logLatest>Loading...</strong></div></div><p class="muted msg" id=logMsg></p></section><section><h2>Waypoints & Routes<button class=card-nav id=openNavTools aria-label="Open Waypoints and Routes">&#x276f;</button></h2><div class=nav-summary id=navSummary>Loading...</div></section></div><div id=navView class=view><div class=subbar><button class=back id=backNavMain aria-label=Back>&#x276e;</button><h2>Waypoints & Routes</h2></div><section><h2>Waypoint Files</h2><div class=waypoint-status-row><div class=nav-summary id=waypointSubStatus>Loading...</div><button class=secondary id=waypointLoad>Import New File</button></div><input id=waypointFile type=file accept=".gpx,.cup,.wpt,.wyp" hidden><div class=file-activate id=waypointActivatePanel><label>Waypoint file</label><select id=waypointFileList></select></div><div class=actions><p class="muted msg" id=waypointMsg></p><button class=hero id=waypointActivate>Activate File</button></div><div id=fileWaypointPanel><label>Waypoint</label><select id=fileWaypointSelect></select><div class=actions><button class=hero id=fileWaypointActivate>Navigate to Point</button><button class=secondary id=fileWaypointMap>Open Map</button></div></div></section><section><h2>User Waypoints</h2><label>Select Waypoint</label><select id=userWaypointSelect></select><div id=userWaypointEditor><div class=row><div><label>Name</label><input id=userWaypointName maxlength=24></div><div class=small><label>&nbsp;</label><button class=hero id=userWaypointRename disabled>Rename</button></div></div><div class=actions><button class=hero id=userWaypointActivate>Navigate to Point</button><button class=secondary id=userWaypointMap>Open Map</button><button class=danger id=userWaypointDelete>Delete</button></div><div class=delete-confirm id=userWaypointDeleteConfirm><p class=delete-warning>Delete saved waypoint?</p><div class=row><button class=danger id=userWaypointConfirmDelete>Confirm Delete</button><button class=hero id=userWaypointCancelDelete>Cancel</button></div></div></div><p class="muted msg" id=userWaypointMsg></p></section><section><h2>Create Route</h2><p class=nav-tools-blocked id=createRouteBlocked>No waypoints available yet.</p><div class=actions id=createRouteLoadPanel><p class="muted msg" id=createRouteLoadMsg></p><button class=hero id=createRouteLoadPoints>Load Points</button></div><div id=createRouteContent><div class=route-edit-list id=editRouteList></div><div class=route-add-grid><div><label id=routeWaypointLabel>First Waypoint</label><select id=routeWaypointList></select></div><div><label>Turn Radius (m)</label><input id=routeDefaultRadius type=number min=10 max=20000 step=10 value=150></div></div><div class=actions><p class="muted msg" id=routeEditHint></p><button class=secondary id=editRouteAdd disabled>Add Point</button></div><label>Route name</label><input id=editRouteName maxlength=48 autocomplete=off><div class="actions route-actions"><label class=checkline><input id=editRouteActivate type=checkbox checked>Set as active route</label><button class=hero id=editRouteSave disabled>Save Route</button></div><p class="muted msg" id=editRouteMsg></p></div></section><section><h2>Import Route</h2><label>Route name</label><input id=routeName maxlength=48 autocomplete=off><label>Task / Route Data</label><textarea id=routeData placeholder="XCTSK:..."></textarea><div class="actions route-actions"><label class=checkline><input id=routeActivate type=checkbox checked>Set as active route</label><button class=hero id=routeSave disabled>Save to Leaf</button></div><p class="muted msg" id=routeMsg></p></section></div><div id=profilesView class=view><div class=subbar><button class=back id=backMain aria-label=Back>&#x276e;</button><h2>Edit Profiles</h2></div><section><h2>Pilots</h2><label>Active pilot</label><select id=pilotList></select><label>Name</label><input id=pilotName maxlength=48 autocomplete=name><label>Email</label><input id=pilotEmail maxlength=80 type=email autocomplete=email><div class=actions><button class=secondary id=leafLogLink disabled>Link Leaf Log</button><button class=secondary id=leafLogWifi>WiFi Setup</button><p class="muted msg" id=leafLogStatus></p></div><div class=leaf-log-panel id=leafLogPanel><div class=leaf-log-step><span>Open Leaf Log and sign in to get a Link Code</span><button class=hero id=leafLogOpen>Open Leaf Log</button></div><label>Leaf Log Link Code</label><input id=leafLogCode maxlength=160 autocomplete=off><div class=actions><p class="muted msg" id=leafLogMsg></p><button class=secondary id=leafLogSave>Save Link</button></div></div><div class="row profile-actions"><button id=pilotSave disabled>Save Profile</button><button class=secondary id=pilotNew>New</button><button class="small danger" id=pilotDelete>Delete</button></div><p class="muted msg" id=pilotMsg></p></section><section><h2>Gliders</h2><label>Active glider</label><select id=gliderList></select><div class=row><div><label>Brand</label><input id=gliderBrand maxlength=32></div><div><label>Model</label><input id=gliderModel maxlength=48></div></div><div class=row><div><label>Size</label><input id=gliderSize maxlength=16></div><div><label>Display name</label><input id=gliderDisplay maxlength=64></div></div><div class="row profile-actions"><button id=gliderSave disabled>Save Profile</button><button class=secondary id=gliderNew>New</button><button class="small danger" id=gliderDelete>Delete</button></div><p class="muted msg" id=gliderMsg></p></section></div><div id=logbookView class=view><div class=subbar><button class=back id=backLogMain aria-label=Back>&#x276e;</button><h2>Logbook</h2></div><section class=flight-card><div class=pager><button id=logPrev>&#x276e;</button><div class=page-title id=logPage>--</div><button id=logNext>&#x276f;</button></div><div class=flight-head><div><span id=flightDay>--</span><strong id=flightDate>Loading...</strong></div><div><span>Start:</span><strong id=flightTime>--</strong></div><div><span>Duration:</span><strong id=flightDuration>--</strong></div></div><div class=flight-profiles><div id=flightPilot></div><div id=flightGlider></div></div><div class=alt-box><div class=alt-title>Altitude</div><div class=alt-row><div class=pill id=altStart><span>Start</span>--</div><div class=pill id=altMax><span>Max</span>--</div><div class=pill id=altEnd><span>End</span>--</div></div></div><div class=detail-grid><div class=vario-box><div class=vario-title>Vario</div><div class=vario-values><div class=pill id=climbMax>--</div><div class="pill sink" id=sinkMax></div></div></div><div class=mini-metrics id=flightMetrics></div></div><div class=track id=trackInfo></div><div class=delete-area><button class=danger id=deleteLog>Delete Log</button><div class=delete-confirm id=deleteConfirm><p class=delete-warning>Delete log and track file?</p><div class=row><button class=danger id=confirmDelete>Confirm Delete</button><button class=hero id=cancelDelete>Cancel</button></div></div></div><p class="muted msg" id=logDetailMsg></p></section></div></main><script>
const LEAF_LOG_ENABLED=false;
let profiles={schema:'leaf.profiles',schema_version:'v0.1.0',active_pilot_id:null,active_glider_id:null,pilots:[],gliders:[]},pilotSnap={},gliderSnap={},navPoints=[],loadedNavFile='',userWaypoints=[],userWaypointSnap='',selectedUserWaypointId='',editRoute=[],logState={prev:'',next:'',path:''},unitPrefs={alt_feet:false,climb_fpm:false,speed_mph:false,distance_miles:false,heading_cardinal:false,temp_f:false,time_12h:false},userStatus={mode:'',mac_address:''};
const $=id=>document.getElementById(id),clean=v=>{v=(v||'').trim();return v?v:null},newId=()=>Math.floor(Math.random()*0xffffffff).toString(16).padStart(8,'0');
function pilotLabel(p){return p.name||'Unnamed pilot'}function gliderLabel(g){return g.display_name||[g.brand,g.model,g.size].filter(Boolean).join(' ')||'Unnamed glider'}
function selectedPilot(){return profiles.pilots.find(p=>p.id==profiles.active_pilot_id)}function selectedGlider(){return profiles.gliders.find(g=>g.id==profiles.active_glider_id)}
function msg(id,t){$(id).textContent=t||''}function validEmail(v){return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test((v||'').trim())}function show(v){$('mainView').classList.toggle('active',v=='main');$('navView').classList.toggle('active',v=='nav');$('profilesView').classList.toggle('active',v=='profiles');$('logbookView').classList.toggle('active',v=='logbook')}
function fillSelect(el,items,label){el.textContent='';items.forEach(x=>{let o=document.createElement('option');o.value=x.id;o.textContent=label(x);el.appendChild(o)});el.disabled=!items.length}
function pilotEditor(){return {id:profiles.active_pilot_id,name:$('pilotName').value||'',email:$('pilotEmail').value||'',leaf_log_api_key:$('leafLogCode').value||''}}function gliderEditor(){return {id:profiles.active_glider_id,brand:$('gliderBrand').value||'',model:$('gliderModel').value||'',size:$('gliderSize').value||'',display_name:$('gliderDisplay').value||''}}
function same(a,b){return JSON.stringify(a)==JSON.stringify(b)}function setSnaps(){pilotSnap=pilotEditor();gliderSnap=gliderEditor();buttons()}
function esc(v){return String(v||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function routeEditButtons(hint=true){let n=clean($('editRouteName').value);$('routeWaypointLabel').textContent=editRoute.length?'Next Waypoint':'First Waypoint';$('editRouteAdd').disabled=!navPoints.length;$('editRouteSave').disabled=!n||!editRoute.length;if(!hint)return;if(!navPoints.length)msg('routeEditHint','Load a waypoint file first.');else msg('routeEditHint','Select a point and add to route.');if(!n&&!editRoute.length)msg('editRouteMsg','Add waypoints and enter route name.');else if(!editRoute.length)msg('editRouteMsg','Add waypoints, then Save Route.');else if(!n)msg('editRouteMsg','Enter route name, then Save Route.');else msg('editRouteMsg','Ready to save route.')}
function mapUrl(p){return 'https://www.google.com/maps/search/?api=1&query='+encodeURIComponent(Number(p.lat).toFixed(7)+','+Number(p.lon).toFixed(7))}
function selectedFilePoint(){let idx=Number($('fileWaypointSelect').value);return navPoints.find(p=>Number(p.index)==idx)}function syncLoadedWaypointFile(){let sel=$('waypointFileList');if(loadedNavFile&&sel.options.length)sel.value=loadedNavFile}function renderWaypointPicker(){let routeEl=$('routeWaypointList'),fileEl=$('fileWaypointSelect');routeEl.textContent='';fileEl.textContent='';navPoints.forEach(p=>{let label=p.name||('Point '+p.index),o=document.createElement('option');o.value=p.index;o.textContent=label;routeEl.appendChild(o);let f=document.createElement('option');f.value=p.index;f.textContent=label;fileEl.appendChild(f)});routeEl.disabled=!navPoints.length;fileEl.disabled=!navPoints.length;$('fileWaypointPanel').style.display=navPoints.length?'block':'none';$('fileWaypointActivate').disabled=!navPoints.length;$('fileWaypointMap').disabled=!navPoints.length;routeEditButtons()}
function selectedUserWaypoint(){return userWaypoints.find(p=>p.id==selectedUserWaypointId)||userWaypoints[0]}
function resetUserWaypointDelete(){$('userWaypointDelete').style.display='block';$('userWaypointDeleteConfirm').style.display='none'}
function userWaypointChanged(){let p=selectedUserWaypoint(),name=$('userWaypointName').value.trim();$('userWaypointRename').disabled=!p||!name||name==(p.name||'')}
function renderUserWaypoints(){let sel=$('userWaypointSelect');sel.textContent='';userWaypoints.forEach(p=>{let o=document.createElement('option');o.value=p.id;o.textContent=p.name||'Saved Point';sel.appendChild(o)});if(!userWaypoints.length){$('userWaypointEditor').style.display='none';msg('userWaypointMsg','Use Save Point on Leaf to add one.');return}let p=selectedUserWaypoint();selectedUserWaypointId=p.id;sel.value=p.id;$('userWaypointEditor').style.display='block';$('userWaypointName').value=p.name||'';userWaypointChanged();resetUserWaypointDelete();msg('userWaypointMsg','')}
async function loadUserWaypoints(){try{let d=await(await fetch('/api/user-waypoints')).json();userWaypoints=d.points||[];if(!userWaypoints.find(p=>p.id==selectedUserWaypointId))selectedUserWaypointId=userWaypoints.length?userWaypoints[0].id:'';userWaypointSnap=JSON.stringify(userWaypoints);renderUserWaypoints()}catch(e){userWaypoints=[];selectedUserWaypointId='';userWaypointSnap='[]';renderUserWaypoints();msg('userWaypointMsg','Unable to read saved points.')}}
async function renameUserWaypoint(){let p=selectedUserWaypoint(),name=$('userWaypointName').value.trim();if(!p||!name)return;msg('userWaypointMsg','Renaming...');try{let r=await fetch('/api/user-waypoints',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({points:[{id:p.id,name:name}]})}),d=await r.json().catch(()=>({}));if(!r.ok||!d.saved)throw d;p.name=name;userWaypointSnap=JSON.stringify(userWaypoints);await loadUserWaypoints();msg('userWaypointMsg','Waypoint renamed.')}catch(x){msg('userWaypointMsg',x&&x.detail?x.detail:'Unable to rename waypoint.')}}
async function deleteUserWaypoint(){let p=selectedUserWaypoint();if(!p)return;msg('userWaypointMsg','Deleting...');try{let r=await fetch('/api/user-waypoints?id='+encodeURIComponent(p.id),{method:'DELETE'}),d=await r.json().catch(()=>({}));if(!r.ok||!d.deleted)throw d;selectedUserWaypointId='';await loadUserWaypoints();msg('userWaypointMsg','Waypoint deleted.')}catch(x){msg('userWaypointMsg',x&&x.detail?x.detail:'Unable to delete waypoint.');resetUserWaypointDelete()}}
function routePointRow(p,i){return '<div class=route-point-row><div class=route-point-name>'+esc(p.name||('Point '+(i+1)))+'</div><input type=number min=10 max=20000 step=10 value="'+(p.radius_m||150)+'" data-r="'+i+'"><button type=button data-act=up data-i="'+i+'">&uarr;</button><button type=button data-act=down data-i="'+i+'">&darr;</button><button type=button data-act=remove data-i="'+i+'">&times;</button></div>'}
function renderEditRoute(){$('editRouteList').innerHTML=editRoute.map(routePointRow).join('');routeEditButtons()}
function navSummaryText(d){let lines=[];lines.push(d.loaded_file||'No waypoint file loaded');lines.push('Points: '+(d.point_count||0)+' Routes: '+(d.route_count||0));if(d.active_name)lines.push('Active '+(d.active_type=='route'?'Route':'Point')+': '+d.active_name);return lines.join('\n')}
function renderNavStatus(d){loadedNavFile=d.loaded_file||'';syncLoadedWaypointFile();$('navSummary').textContent=navSummaryText(d);$('waypointSubStatus').textContent=navSummaryText(d);let ready=!!navPoints.length,canLoad=!ready&&!!(d.point_count||0);$('createRouteBlocked').textContent=ready?'':(canLoad?'Waypoint file loaded. Load points to create a route.':'No waypoints available yet.');$('createRouteBlocked').style.display=ready?'none':'block';$('createRouteLoadPanel').style.display=canLoad?'flex':'none';$('createRouteContent').style.display=ready?'block':'none'}
let navDataQueue=Promise.resolve(),navDataSeq=0,navDataFullSeq=0;
function navDataFresh(includePoints,seq){return seq==navDataSeq||(includePoints&&seq==navDataFullSeq)}
async function loadNavData(includePoints=false){let seq=++navDataSeq;if(includePoints)navDataFullSeq=seq;navDataQueue=navDataQueue.catch(()=>{}).then(()=>loadNavDataSerial(includePoints,seq));return navDataQueue}
async function loadNavDataSerial(includePoints,seq){if(!navDataFresh(includePoints,seq))return false;try{let r=await fetch('/api/nav-data'+(includePoints?'?points=1':'')),d=await r.json().catch(()=>({}));if(!r.ok||d.points_unavailable)throw d;if(!navDataFresh(includePoints,seq))return false;if(includePoints)navPoints=d.points||[];renderNavStatus(d);if(includePoints)renderWaypointPicker();return true}catch(e){if(!navDataFresh(includePoints,seq))return false;if(includePoints){try{let d=await(await fetch('/api/nav-data')).json();if(!navDataFresh(includePoints,seq))return false;renderNavStatus(d);renderWaypointPicker();msg('routeEditHint','Unable to refresh waypoint list.');msg('waypointMsg','Unable to refresh waypoint list.');return false}catch(x){}}$('navSummary').textContent='Unable to read navigation data.';$('waypointSubStatus').textContent='Unable to read navigation data.';if(includePoints)msg('routeEditHint','Unable to refresh waypoint list.');else msg('routeEditHint','Unable to read waypoints.');return false}}
async function loadRoutePoints(){msg('createRouteLoadMsg','Loading points...');$('createRouteLoadPoints').disabled=true;let ok=await loadNavData(true);msg('createRouteLoadMsg',ok?(navPoints.length?('Loaded '+navPoints.length+' points.'):'No points available.'):'Unable to refresh waypoint list.');$('createRouteLoadPoints').disabled=false}
async function activatePointIndex(index,msgId){if(!index){msg(msgId,'Point is not loaded yet.');return}msg(msgId,'Activating point...');try{let r=await fetch('/api/nav/activate-point',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:index})}),d=await r.json().catch(()=>({}));if(!r.ok||!d.active)throw d;msg(msgId,'Active: '+(d.name||'point'));loadNavData(!!navPoints.length)}catch(x){msg(msgId,x&&x.detail?x.detail:'Unable to activate point.')}}
async function loadWaypointFileList(){msg('waypointMsg','Loading files...');try{let d=await(await fetch('/api/waypoints/files')).json(),files=d.files||[],sel=$('waypointFileList'),current=loadedNavFile||sel.value;sel.textContent='';files.forEach(f=>{let o=document.createElement('option');o.value=f.name;o.textContent=f.name;sel.appendChild(o)});if(current)sel.value=current;msg('waypointMsg',files.length?'Choose a file, then Activate File.':'No waypoint files found.')}catch(e){msg('waypointMsg','Unable to list waypoint files.')}}async function activateWaypointFile(){let sel=$('waypointFileList');if(!sel.options.length)await loadWaypointFileList();let name=sel.value;if(!name){msg('waypointMsg','Choose a waypoint file.');return}msg('waypointMsg','Activating '+name+'...');try{let r=await fetch('/api/waypoints/activate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:name})}),d=await r.json().catch(()=>({}));if(!r.ok||!d.loaded)throw d;msg('waypointMsg','Activated '+(d.filename||name)+'.');await loadNavData(true);await loadUserWaypoints()}catch(x){msg('waypointMsg',x&&x.detail?x.detail:'Unable to activate file.')}}
function addSelectedRoutePoint(){let idx=Number($('routeWaypointList').value),p=navPoints.find(x=>Number(x.index)==idx);if(!p)return;let radius=Math.max(10,Math.min(20000,Math.round(Number($('routeDefaultRadius').value)||150)));$('routeDefaultRadius').value=radius;editRoute.push({name:p.name,lat:p.lat,lon:p.lon,alt_m:p.alt_m||0,radius_m:radius});renderEditRoute()}
async function saveEditedRoute(){let name=clean($('editRouteName').value);if(!name||!editRoute.length){routeEditButtons();return}msg('editRouteMsg','Saving route...');$('editRouteSave').disabled=true;try{let r=await fetch('/api/routes/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:name,activate:$('editRouteActivate').checked,points:editRoute})}),d=await r.json().catch(()=>({}));if(!r.ok||!d.saved)throw d;msg('editRouteMsg','Saved '+(d.points||0)+' points'+(d.active?' and set active.':'.'))}catch(x){msg('editRouteMsg',x&&x.detail?x.detail:'Unable to save route.')}routeEditButtons(false)}function routeButtons(hint=true){let n=clean($('routeName').value),d=clean($('routeData').value);$('routeSave').disabled=!n||!d;if(!hint)return;if(!n&&!d)msg('routeMsg','Enter route name and data, then Save to Leaf.');else if(!n)msg('routeMsg','Enter route name, then Save to Leaf.');else if(!d)msg('routeMsg','Paste route data, then Save to Leaf.');else msg('routeMsg','Ready to save route.')}async function uploadWaypointFile(file){if(!file)return;msg('waypointMsg','Uploading '+file.name+'...');$('waypointLoad').disabled=true;let form=new FormData();form.append('file',file,file.name);try{let r=await fetch('/api/waypoints/upload',{method:'POST',body:form}),d=await r.json().catch(()=>({}));if(!d.saved)throw d;let counts=(d.points||0)+' pts';if(d.routes)counts+=', '+d.routes+' rtes';if(d.loaded){msg('waypointMsg','Loaded '+(d.filename||file.name)+': '+counts+'.');await loadNavData(true);await loadWaypointFileList()}else msg('waypointMsg','Saved '+(d.filename||file.name)+', but Leaf could not load it.')}catch(x){msg('waypointMsg',x&&x.detail?x.detail:'Unable to load file.')} $('waypointLoad').disabled=false}function buttons(){let p=pilotEditor(),g=gliderEditor();$('pilotSave').disabled=!clean(p.name)||same(p,pilotSnap);$('pilotDelete').disabled=!selectedPilot();$('pilotNew').disabled=!profiles.pilots.length;$('gliderSave').disabled=!([g.brand,g.model,g.size,g.display_name].some(v=>clean(v)))||same(g,gliderSnap);$('gliderDelete').disabled=!selectedGlider();$('gliderNew').disabled=!profiles.gliders.length;leafLogButtons();routeButtons()}
function render(){fillSelect($('pilotList'),profiles.pilots,pilotLabel);fillSelect($('activePilotList'),profiles.pilots,pilotLabel);if(profiles.active_pilot_id){$('pilotList').value=profiles.active_pilot_id;$('activePilotList').value=profiles.active_pilot_id}let p=selectedPilot();$('pilotName').value=p?p.name||'':'';$('pilotEmail').value=p?p.email||'':'';$('leafLogCode').value=p?p.leaf_log_api_key||'':'';$('leafLogPanel').classList.remove('active');msg('leafLogMsg','');if(!profiles.pilots.length)msg('pilotMsg','Enter pilot name, then Save Profile.');fillSelect($('gliderList'),profiles.gliders,gliderLabel);fillSelect($('activeGliderList'),profiles.gliders,gliderLabel);if(profiles.active_glider_id){$('gliderList').value=profiles.active_glider_id;$('activeGliderList').value=profiles.active_glider_id}let g=selectedGlider();$('gliderBrand').value=g?g.brand||'':'';$('gliderModel').value=g?g.model||'':'';$('gliderSize').value=g?g.size||'':'';$('gliderDisplay').value=g?g.display_name||'':'';if(!profiles.gliders.length)msg('gliderMsg','Enter glider details, then Save Profile.');setSnaps()}
function normalize(){profiles.schema='leaf.profiles';profiles.schema_version='v0.1.0';profiles.pilots=(profiles.pilots||[]).filter(p=>p&&p.id&&p.name);profiles.gliders=(profiles.gliders||[]).filter(g=>g&&g.id&&g.model);if(!profiles.pilots.find(p=>p.id==profiles.active_pilot_id))profiles.active_pilot_id=profiles.pilots.length==1?profiles.pilots[0].id:null;if(!profiles.gliders.find(g=>g.id==profiles.active_glider_id))profiles.active_glider_id=profiles.gliders.length==1?profiles.gliders[0].id:null}
async function save(){normalize();let r=await fetch('/api/profiles',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(profiles)});if(!r.ok)throw new Error();render()}
function leafLogLinked(){return !!clean($('leafLogCode').value)}function leafLogButtons(){let row=$('leafLogLink').parentElement;if(!LEAF_LOG_ENABLED){row.style.display='none';$('leafLogPanel').classList.remove('active');msg('leafLogStatus','');msg('leafLogMsg','');return}row.style.display='flex';let p=pilotEditor(),saved=!!selectedPilot(),email=validEmail(p.email),network=userStatus.mode=='network',linked=leafLogLinked();$('leafLogWifi').style.display=network?'none':'block';$('leafLogLink').disabled=!saved||!email||!network;if(!saved)msg('leafLogStatus','Save this pilot profile before linking Leaf Log.');else if(!email)msg('leafLogStatus','Add the email you use for Leaf Log.');else if(!network)msg('leafLogStatus','Join a Wifi network to link');else msg('leafLogStatus',linked?'Linked':'Not linked')}function leafLogUrl(){let q=new URLSearchParams({email:clean($('pilotEmail').value)||'',mac:userStatus.mac_address||'',profile:profiles.active_pilot_id||''});return 'https://leaflogonline.com/link?'+q.toString()}function versionText(s){let fw=s.firmware_display_version||'',hw=s.hardware_display_version||'';if(!fw){let a=(s.firmware_version||'').split('+');fw=a[0]||'unknown';hw=a[1]||hw||'';if(fw[0]!='v')fw='v'+fw;if(hw&&hw[0]=='h')hw=hw.slice(1);if(hw&&hw[0]!='v')hw='v'+hw}return `firmware: ${fw}`+(hw?`\nhardware: ${hw}`:'')}
async function checkFirmware(){let b=$('firmwareCheck');msg('firmwareCheckMsg','checking...');b.disabled=true;try{let r=await fetch('/api/firmware/update-status',{method:'POST'}),d=await r.json().catch(()=>({}));if(!r.ok||!d.ok)throw d;msg('firmwareCheckMsg',d.message||'')}catch(x){msg('firmwareCheckMsg','unable to check for updates')}b.disabled=false}function useUnits(u){if(u)unitPrefs=u}function logParts(t,h12){if(!t)return{day:'--',date:'--',time:'--'};let a=t.split('T'),d=a[0]||'--',hm=(a[1]||'').slice(0,5)||'--',dt=new Date(t),day=isNaN(dt)?'--':dt.toLocaleDateString('en-US',{weekday:'long'}),date=isNaN(dt)?d:dt.toLocaleDateString('en-GB',{day:'numeric',month:'short',year:'numeric'}),h=Number(hm.slice(0,2)),m=hm.slice(3,5);if(h12&&Number.isFinite(h)){let ap=h>=12?'PM':'AM';h=h%12||12;hm=h+':'+m+'\u00a0'+ap}return{day:day,date:date,time:hm}}function logDateTime(t,h12){let p=logParts(t,h12);return p.date?(p.date+'  '+p.time):''}
function dur(s){s=Number(s)||0;let h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h?h+'h '+m+'m':m+'m'}
function good(v){return v!==null&&v!==undefined&&v!==""&&Number.isFinite(Number(v))}function m(v){if(!good(v))return'--';v=Number(v);return unitPrefs.alt_feet?Math.round(v*3.28084)+' ft':Math.round(v)+' m'}function ms(v){if(!good(v))return'--';v=Number(v);return unitPrefs.climb_fpm?Math.round(v*196.85)+' fpm':v.toFixed(1)+' m/s'}function spd(v){if(!good(v))return'--';v=Number(v);return unitPrefs.speed_mph?(v*2.23694).toFixed(1)+' mph':(v*3.6).toFixed(1)+' kph'}function windSpd(v){if(!good(v))return'--';v=Number(v);return unitPrefs.speed_mph?Math.round(v*2.23694)+' mph':Math.round(v*3.6)+' kph'}function dist(v){if(!good(v))return'--';v=Number(v);if(unitPrefs.distance_miles)return v>805?(v*0.000621371).toFixed(2)+' mi':Math.round(v*3.28084)+' ft';return v>=1000?(v/1000).toFixed(2)+' km':Math.round(v)+' m'}function tempVal(c){if(!good(c))return'--';c=Number(c);return unitPrefs.temp_f?Math.round(c*9/5+32):Math.round(c)}function tempRange(a,b){return good(a)&&good(b)?tempVal(a)+'\u00b0 / '+tempVal(b)+'\u00b0'+(unitPrefs.temp_f?'F':'C'):'--'}function hdg(d){if(!good(d))return'--';d=(Math.round(Number(d))%360+360)%360;if(!unitPrefs.heading_cardinal)return d+' deg';let a=['N','NNE','NE','ENE','E','ESE','SE','SSE','S','SSW','SW','WSW','W','WNW','NW','NNW'];return a[Math.round(d/22.5)%16]}function wind(v,d){return windSpd(v)+' '+hdg(d)}
function altShown(v){if(!good(v))return NaN;v=Number(v);return unitPrefs.alt_feet?Math.round(v*3.28084):Math.round(v)}
function renderAlt(e){let s=Number(e.start_altitude_m),x=Number(e.max_altitude_m),n=Number(e.min_altitude_m),end=Number(e.end_altitude_m);let vals=[s,x,end].filter(Number.isFinite),a=$('altStart'),b=$('altMax'),c=$('altEnd');[a,b,c].forEach(q=>{q.classList.remove('high');q.style.display='none'});if(!vals.length)return;if(!Number.isFinite(n))n=Math.min(...vals,0);let hi=Math.max(...vals,0),lo=Math.min(n,...vals,0),range=Math.max(1,hi-lo),top=v=>Number.isFinite(v)?Math.round((hi-v)/range*64)+2:32,sv=Number.isFinite(s),ev=Number.isFinite(end),showMax=Number.isFinite(x)&&altShown(x)>Math.max(sv?altShown(s):-1e9,ev?altShown(end):-1e9);if(sv){a.style.display='block';a.lastChild.textContent=m(s);a.style.left='0';a.style.top=top(s)+'px'}if(showMax){b.style.display='block';b.lastChild.textContent=m(x);b.style.left='50%';b.style.transform='translateX(-50%)';b.style.top=top(x)+'px'}if(ev){c.style.display='block';c.lastChild.textContent=m(end);c.style.right='0';c.style.top=top(end)+'px'}if(showMax)b.classList.add('high');else if(sv&&(!ev||altShown(s)>=altShown(end)))a.classList.add('high');else if(ev)c.classList.add('high')}
async function loadStatus(){try{let s=await(await fetch('/api/user/status')).json();userStatus=s;$('status').textContent=`mode: ${s.mode}\nssid: ${s.ssid||'(none)'}\nip: ${s.ip_address||'(none)'}\n`+versionText(s);leafLogButtons();let fwRow=$("firmwareCheckRow"),fwVisible=s.mode=="network";fwRow.style.display=fwVisible?"flex":"none";if(!fwVisible)msg("firmwareCheckMsg","");if(Number.isFinite(Number(s.battery_percent))){let p=Math.max(0,Math.min(100,Number(s.battery_percent)));$('batteryFill').style.width=p+'%';$('batteryText').textContent=p+'%';$('batteryCharge').textContent=s.battery_charging?'Charging':'Not charging'}else $('batteryBox').style.display='none'}catch(e){$('status').textContent='Unable to read status.';$('firmwareCheckRow').style.display='none'}}
async function loadLogbook(){try{let d=await(await fetch('/api/logbook')).json();useUnits(d.units);$('logCount').textContent=d.count||0;$('logLatest').textContent=d.latest&&d.latest.start_time_local?logDateTime(d.latest.start_time_local,unitPrefs.time_12h):(d.count?'Unknown':'No flights')}catch(e){$('logLatest').textContent='Unavailable'}}
function resetDelete(){$('deleteLog').style.display='block';$('deleteConfirm').style.display='none'}
function clearLogCard(d){logState={prev:d&&d.previous_path||'',next:d&&d.next_path||'',path:d&&d.path||''};$('logPage').textContent=((d&&d.position)||'--')+'/'+((d&&d.total)||'--');$('logPrev').disabled=!logState.next;$('logNext').disabled=!logState.prev;$('flightDay').textContent='--';$('flightDate').textContent='Date unknown';$('flightTime').textContent='--';$('flightDuration').textContent='--';$('flightPilot').textContent='';$('flightGlider').textContent='';renderAlt({});$('climbMax').style.display='none';$('sinkMax').style.display='none';$('flightMetrics').innerHTML=['Straight Dist','Path Dist','Max Speed','Accel','Temp'].map(x=>`<div class=mini-row><span>${x}</span><strong>--</strong></div>`).join('');$('trackInfo').textContent=(d&&d.filename?'Bad log: '+d.filename:'Bad log');$('deleteLog').disabled=!logState.path}
async function loadLogEntry(path){msg('logDetailMsg','Loading...');resetDelete();$('deleteLog').disabled=false;let url='/api/logbook/entry'+(path?'?path='+encodeURIComponent(path):'');try{let r=await fetch(url),d=await r.json().catch(()=>({}));useUnits(d.units);if(!r.ok||!d.ok){clearLogCard(d);throw d}let e=d.entry;logState={prev:d.previous_path||'',next:d.next_path||'',path:e.path||''};$('logPage').textContent=(d.position||'--')+'/'+(d.total||'--');$('logPrev').disabled=!logState.next;$('logNext').disabled=!logState.prev;let lp=logParts(e.start_time_local,unitPrefs.time_12h);$('flightDay').textContent=lp.day||'--';$('flightDate').textContent=lp.date||'Date unknown';$('flightTime').textContent=lp.time||'--';$('flightDuration').textContent=dur(e.duration_seconds);$('flightPilot').textContent=e.pilot_name||'';$('flightGlider').textContent=e.glider_display_name||'';renderAlt(e);$('climbMax').style.display=good(e.max_climb_rate_mps)?'block':'none';$('sinkMax').style.display=good(e.max_sink_rate_mps)?'block':'none';$('climbMax').textContent=ms(e.max_climb_rate_mps);$('sinkMax').textContent=ms(e.max_sink_rate_mps);let rows=[['Straight Dist',dist(e.straight_line_distance_m)],['Path Dist',dist(e.path_distance_m)],['Max Speed',spd(e.max_ground_speed_mps)],['Accel',(good(e.min_accel_g)&&good(e.max_accel_g)?Number(e.min_accel_g).toFixed(1)+' / '+Number(e.max_accel_g).toFixed(1)+' G':'--')],['Temp',tempRange(e.min_temperature_c,e.max_temperature_c)]];if(e.max_wind_valid)rows.push(['Wind',wind(e.max_wind_speed_mps,e.max_wind_direction_from_deg)]);$('flightMetrics').innerHTML=rows.map(r=>`<div class=mini-row><span>${r[0]}</span><strong>${r[1]}</strong></div>`).join('');let tn=e.track_path?e.track_path.split('/').pop():'';$('trackInfo').innerHTML='<span>'+(e.track_saved?('Track File: <span class=track-file-name>'+tn+'</span>'):'No track file')+'</span><span class=flight-id>Flight ID '+(e.flight_id||'--')+'</span>';$('deleteLog').disabled=!logState.path;msg('logDetailMsg','')}catch(x){resetDelete();if(!(x&&x.path))$('deleteLog').disabled=true;msg('logDetailMsg',x&&x.detail?x.detail:'Unable to load log.')}}
async function loadProfiles(){try{profiles=await(await fetch('/api/profiles')).json();normalize();msg('pilotMsg','');msg('gliderMsg','');render()}catch(e){msg('pilotMsg','Unable to read profiles.')}}
$('firmwareCheck').onclick=checkFirmware;$('openProfiles').onclick=()=>show('profiles');$('backMain').onclick=()=>show('main');$('openNavTools').onclick=async()=>{show('nav');await loadUserWaypoints();await loadNavData(true);await loadWaypointFileList()};$('backNavMain').onclick=()=>show('main');$('backLogMain').onclick=()=>show('main');$('openLogbook').onclick=()=>{show('logbook');loadLogEntry('')};$('waypointActivate').onclick=activateWaypointFile;$('waypointLoad').onclick=()=>{$('waypointFile').value='';$('waypointFile').click()};$('waypointFile').onchange=()=>uploadWaypointFile($('waypointFile').files[0]);$('fileWaypointActivate').onclick=()=>{let p=selectedFilePoint();if(p)activatePointIndex(Number(p.index),'waypointMsg')};$('fileWaypointMap').onclick=()=>{let p=selectedFilePoint();if(p)window.open(mapUrl(p),'_blank','noopener')};$('userWaypointSelect').onchange=e=>{selectedUserWaypointId=e.target.value;renderUserWaypoints()};$('userWaypointName').oninput=userWaypointChanged;$('userWaypointRename').onclick=renameUserWaypoint;$('userWaypointActivate').onclick=()=>{let p=selectedUserWaypoint();if(p)activatePointIndex(Number(p.index),'userWaypointMsg')};$('userWaypointMap').onclick=()=>{let p=selectedUserWaypoint();if(p)window.open(mapUrl(p),'_blank','noopener')};$('userWaypointDelete').onclick=()=>{if(!selectedUserWaypoint())return;$('userWaypointDelete').style.display='none';$('userWaypointDeleteConfirm').style.display='block';msg('userWaypointMsg','')};$('userWaypointCancelDelete').onclick=resetUserWaypointDelete;$('userWaypointConfirmDelete').onclick=deleteUserWaypoint;$('createRouteLoadPoints').onclick=loadRoutePoints;$('editRouteName').oninput=routeEditButtons;$('editRouteAdd').onclick=addSelectedRoutePoint;$('editRouteSave').onclick=saveEditedRoute;$('editRouteList').onclick=e=>{let b=e.target.closest('button');if(!b)return;let i=Number(b.dataset.i),a=b.dataset.act;if(a=='remove')editRoute.splice(i,1);else if(a=='up'&&i>0)[editRoute[i-1],editRoute[i]]=[editRoute[i],editRoute[i-1]];else if(a=='down'&&i<editRoute.length-1)[editRoute[i+1],editRoute[i]]=[editRoute[i],editRoute[i+1]];renderEditRoute()};$('editRouteList').oninput=e=>{if(e.target.dataset.r===undefined)return;let i=Number(e.target.dataset.r),v=Number(e.target.value)||150;editRoute[i].radius_m=Math.max(10,Math.min(20000,Math.round(v)))};$('logPrev').onclick=()=>{if(logState.next)loadLogEntry(logState.next)};$('logNext').onclick=()=>{if(logState.prev)loadLogEntry(logState.prev)};$('deleteLog').onclick=()=>{if(!logState.path)return;$('deleteLog').style.display='none';$('deleteConfirm').style.display='block';msg('logDetailMsg','')};$('cancelDelete').onclick=resetDelete;$('confirmDelete').onclick=async()=>{if(!logState.path)return;msg('logDetailMsg','Deleting...');try{let r=await fetch('/api/logbook/entry?path='+encodeURIComponent(logState.path),{method:'DELETE'}),d=await r.json().catch(()=>({}));if(!r.ok||!d.ok)throw d;await loadLogbook();if(d.count>0)loadLogEntry(d.next_path||'');else{show('main');msg('logMsg','Log deleted.')}}catch(x){msg('logDetailMsg',x&&x.detail?x.detail:'Unable to delete log.');resetDelete()}};['pilotName','pilotEmail','leafLogCode','gliderBrand','gliderModel','gliderSize','gliderDisplay'].forEach(x=>$(x).oninput=buttons);['routeName','routeData'].forEach(x=>$(x).oninput=routeButtons);$('leafLogLink').onclick=()=>{$('leafLogPanel').classList.add('active');msg('leafLogMsg','')};$('leafLogWifi').onclick=()=>{location.href='/wifi?scan=1&return=app'};$('leafLogOpen').onclick=()=>{window.open(leafLogUrl(),'_blank','noopener')};$('leafLogSave').onclick=()=>{let name=clean($('pilotName').value);if(!name){msg('leafLogMsg','Pilot name is required.');return}let p=selectedPilot();if(!p){msg('leafLogMsg','Save this pilot profile before linking Leaf Log.');return}p.name=name;p.email=clean($('pilotEmail').value);p.leaf_log_api_key=clean($('leafLogCode').value);save().then(()=>{msg('leafLogMsg','Leaf Log link saved.');msg('pilotMsg','Pilot profile saved.')}).catch(()=>msg('leafLogMsg','Unable to save Leaf Log link.'))};$('routeSave').onclick=async()=>{let name=clean($('routeName').value),data=clean($('routeData').value);if(!name||!data){routeButtons();return}msg('routeMsg','Saving route...');$('routeSave').disabled=true;try{let r=await fetch('/api/routes/import',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:name,data:data,activate:$('routeActivate').checked})}),d=await r.json().catch(()=>({}));if(!r.ok||!d.saved)throw d;msg('routeMsg','Saved '+(d.points||0)+' points'+(d.active?' and set active.':'.'));$('routeData').value=''}catch(x){msg('routeMsg',x&&x.detail?x.detail:'Unable to save route.')}routeButtons(false)};
$('activePilotList').onchange=()=>{profiles.active_pilot_id=$('activePilotList').value;render();save().then(()=>msg('mainProfileMsg','Active pilot saved.')).catch(()=>msg('mainProfileMsg','Unable to save.'))};
$('activeGliderList').onchange=()=>{profiles.active_glider_id=$('activeGliderList').value;render();save().then(()=>msg('mainProfileMsg','Active glider saved.')).catch(()=>msg('mainProfileMsg','Unable to save.'))};
$('pilotList').onchange=()=>{profiles.active_pilot_id=$('pilotList').value;render();save().then(()=>msg('pilotMsg','Active pilot selected.')).catch(()=>msg('pilotMsg','Unable to save.'))};
$('gliderList').onchange=()=>{profiles.active_glider_id=$('gliderList').value;render();save().then(()=>msg('gliderMsg','Active glider selected.')).catch(()=>msg('gliderMsg','Unable to save.'))};
$('pilotNew').onclick=()=>{profiles.active_pilot_id=null;$('pilotName').value='';$('pilotEmail').value='';$('leafLogCode').value='';$('leafLogPanel').classList.remove('active');setSnaps();$('pilotName').focus();msg('pilotMsg','Enter pilot name, then Save Profile.')};
$('gliderNew').onclick=()=>{profiles.active_glider_id=null;['gliderBrand','gliderModel','gliderSize','gliderDisplay'].forEach(x=>$(x).value='');setSnaps();$('gliderModel').focus();msg('gliderMsg','Enter glider details, then Save Profile.')};
$('pilotSave').onclick=()=>{let name=clean($('pilotName').value);if(!name){msg('pilotMsg','Pilot name is required.');return}let p=selectedPilot();if(!p){p={id:newId(),name:''};profiles.pilots.push(p);profiles.active_pilot_id=p.id}p.name=name;p.email=clean($('pilotEmail').value);p.leaf_log_api_key=clean($('leafLogCode').value);save().then(()=>msg('pilotMsg','Pilot profile saved.')).catch(()=>msg('pilotMsg','Unable to save pilot.'))};
$('gliderSave').onclick=()=>{let model=clean($('gliderModel').value);if(!model){msg('gliderMsg','Glider model is required.');return}let g=selectedGlider();if(!g){g={id:newId(),model:''};profiles.gliders.push(g);profiles.active_glider_id=g.id}g.brand=clean($('gliderBrand').value);g.model=model;g.size=clean($('gliderSize').value);g.display_name=clean($('gliderDisplay').value);save().then(()=>msg('gliderMsg','Glider profile saved.')).catch(()=>msg('gliderMsg','Unable to save glider.'))};
$('pilotDelete').onclick=()=>{let p=selectedPilot();if(!p)return;profiles.pilots=profiles.pilots.filter(x=>x.id!=p.id);profiles.active_pilot_id=null;save().then(()=>msg('pilotMsg','Pilot profile deleted.')).catch(()=>msg('pilotMsg','Unable to delete pilot.'))};
$('gliderDelete').onclick=()=>{let g=selectedGlider();if(!g)return;profiles.gliders=profiles.gliders.filter(x=>x.id!=g.id);profiles.active_glider_id=null;save().then(()=>msg('gliderMsg','Glider profile deleted.')).catch(()=>msg('gliderMsg','Unable to delete glider.'))};
leafLogButtons();routeButtons();routeEditButtons();loadStatus();loadProfiles();loadLogbook();loadNavData();loadUserWaypoints();
</script></body></html>)leafapp";
    sendNoStoreHeaders(target);
    if (!settings.dev_mode) {
      target.send_P(200, "text/html", USER_APP_PAGE);
      return;
    }

    static constexpr char USER_APP_DEVELOPER_SCRIPT[] PROGMEM =
        R"leafdev(<script>
(function(){
function el(t,a,c){let n=document.createElement(t);if(a)Object.keys(a).forEach(k=>{if(k=="text")n.textContent=a[k];else n.setAttribute(k,a[k])});(c||[]).forEach(x=>n.appendChild(x));return n}
function row(label,id){let l=el("label",{class:"checkline"}),i=el("input",{id:id,type:"checkbox"}),s=el("span",{text:label});l.appendChild(i);l.appendChild(s);return l}
function msg(t){let m=document.getElementById("devMsg");if(m)m.textContent=t||""}
let main=document.getElementById("mainView");if(!main)return;
let s=el("section",{id:"developerCard"}),h=el("h2",{text:"Developer"}),status=el("div",{class:"status",id:"devStatus",text:"Loading..."}),actions=el("div",{class:"actions"}),msgEl=el("p",{class:"muted msg",id:"devMsg"});
let shot=el("button",{class:"hero",id:"devScreenshot",text:"Screenshot"}),msc=el("button",{class:"secondary",id:"devMassStorage",text:"Mass Storage"}),mem=el("button",{class:"secondary",id:"devMemory",text:"Memory"}),fan=el("button",{class:"secondary",id:"devFanet",text:"FANET"});
actions.style.flexWrap="wrap";
actions.appendChild(shot);actions.appendChild(msc);actions.appendChild(mem);actions.appendChild(fan);
s.appendChild(h);s.appendChild(status);s.appendChild(row("Keep web app on","devAlwaysOn"));s.appendChild(row("Keep Bluetooth disabled","devKeepBleDisabled"));s.appendChild(actions);s.appendChild(msgEl);main.appendChild(s);
async function load(){try{let r=await Promise.all([fetch("/api/debug/session"),fetch("/api/user/status")]),d=await r[0].json(),u=await r[1].json();document.getElementById("devAlwaysOn").checked=!!d.always_on;document.getElementById("devKeepBleDisabled").checked=!!d.keep_bluetooth_disabled;status.textContent="web app: "+(d.web_app_active?"active":"off")+"\\nmode: "+(d.using_leaf_wifi?"Leaf AP":"network")+"\\nfirmware: "+(u.firmware_display_version||u.firmware_version||"")+"\\nmac: "+(u.mac_address||"")}catch(e){status.textContent="Developer controls unavailable."}}
async function save(){msg("Saving...");try{let body={always_on:document.getElementById("devAlwaysOn").checked,keep_bluetooth_disabled:document.getElementById("devKeepBleDisabled").checked};await fetch("/api/debug/session",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});msg("Saved for this session.");load()}catch(e){msg("Unable to save developer settings.")}}
document.getElementById("devAlwaysOn").onchange=save;document.getElementById("devKeepBleDisabled").onchange=save;
shot.onclick=()=>window.open("/app/debug/screenshot","_blank","noopener");
mem.onclick=()=>window.open("/app/debug/memory","_blank","noopener");
fan.onclick=()=>window.open("/app/debug/fanet","_blank","noopener");
msc.onclick=async()=>{if(!confirm("Start SD mass storage?"))return;msg("Starting mass storage...");try{await fetch("/api/debug/mass-storage");msg("Mass storage requested.")}catch(e){msg("Unable to start mass storage.")}};
load();
})();
</script>)leafdev";
    target.setContentLength(CONTENT_LENGTH_UNKNOWN);
    target.send(200, "text/html", "");
    target.sendContent_P(USER_APP_PAGE);
    target.sendContent_P(USER_APP_DEVELOPER_SCRIPT);
    target.sendContent("");
    return;
  }

  void sendWifiSetupPage(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "text/plain", "Leaf Web App is not active.");
      return;
    }

    sendNoStoreHeaders(target);
    static constexpr char WIFI_SETUP_PAGE[] PROGMEM =
        R"leafhtml(<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><meta http-equiv=Cache-Control content=no-store><title>Leaf WiFi</title><style>:root{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#202423;background:#363636;line-height:1.35;--leaf:#d8ff00;--ink:#202423;--panel:#565656;--sub:#4d4d4d}body{margin:0;background:#363636}header{background:var(--leaf);color:#0b0d0b;padding:11px 20px;text-align:center}main{max-width:640px;margin:auto;padding:18px}h1{font-family:Arial,sans-serif;font-size:38px;font-weight:500;letter-spacing:.12em;line-height:1;margin:0}.subbar{display:flex;align-items:center;justify-content:center;min-height:34px;color:white;margin:0 0 14px}.subbar h2{color:white;background:transparent;margin:0;padding:0;font-size:18px}section{background:var(--panel);border-radius:8px;margin:0 0 14px;padding:16px 14px}label{display:block;font-size:13px;font-weight:700;margin:10px 0 4px;color:white}input,select,button{box-sizing:border-box;width:100%;font:inherit;padding:11px;border:1px solid #b9c0b2;border-radius:7px;background:white;color:var(--ink)}input:focus,select:focus,button:focus{outline:2px solid var(--leaf);outline-offset:1px}button{background:var(--ink);color:white;font-weight:750;border-color:var(--ink);box-shadow:inset 0 -2px 0 rgba(0,0,0,.22)}button:disabled{background:#686868;border-color:#686868;color:#8a8a8a;opacity:1;box-shadow:none}.hero:not(:disabled){background:var(--leaf);border-color:var(--leaf);color:#0b0d0b}.status{min-height:20px;margin:0 0 12px;color:#e2e7dc}.network-row,.row{display:flex;gap:8px}.network-row select,.row input{flex:1}.refresh{flex:0 0 44px;width:44px;height:44px;padding:0;font-size:23px;line-height:1}.show{display:flex;align-items:center;gap:6px;width:auto;white-space:nowrap;color:white;font-weight:700}.show input{width:auto;accent-color:var(--leaf)}#n{margin-top:8px}#save{margin-top:16px}</style><script>async function init(){let s=document.getElementById('s'),l=document.getElementById('l'),n=document.getElementById('n'),p=document.getElementById('p'),w=document.getElementById('w'),f=document.getElementById('f'),r=document.getElementById('r'),save=document.getElementById('save');function chosen(){let o=l.options[l.selectedIndex];return o&&o.value&&o.value==n.value.trim()?o:null}function buttons(){let o=chosen(),need=o&&o.dataset.secure=='1';save.disabled=!n.value.trim()||(need&&!p.value)}l.onchange=()=>{if(l.value)n.value=l.value;buttons()};n.oninput=buttons;p.oninput=buttons;w.onchange=()=>p.type=w.checked?'text':'password';r.onclick=()=>nets(true);let params=new URLSearchParams(location.search),autoScan=params.has('scan');async function nets(refresh){try{let d=await(await fetch('/api/wifi/networks'+(refresh?'?refresh=1':''))).json();if(d.scanning){s.textContent='Scanning for networks...';setTimeout(nets,1500);return}l.innerHTML='<option value="">Select network...</option>';d.networks.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.dataset.secure=x.secure?'1':'0';o.textContent=x.ssid+' ('+x.rssi+' dBm)';l.appendChild(o)});s.textContent=d.networks.length?'Choose a network or type one manually.':'No networks found. Type the network name manually.';buttons()}catch(e){s.textContent='Unable to read network list. Type the network name manually.';buttons()}}async function poll(){try{let d=await(await fetch('/api/wifi/status')).json();if(d.connected){f.style.display='none';s.textContent='Connected to '+d.ssid+'. To use the Leaf Web App, open Menu > Web App on Leaf.';return}if(d.error){s.textContent='Unable to connect. Check password and try again.';return}s.textContent=d.target_ssid?'Trying to connect to '+d.target_ssid+'...':'Trying to connect...';setTimeout(poll,1500)}catch(e){s.textContent='Trying to connect...';setTimeout(poll,2000)}}f.onsubmit=async e=>{e.preventDefault();let ssid=n.value.trim();if(save.disabled)return;s.textContent='Saving credentials...';try{await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:p.value})});save.disabled=true;poll()}catch(x){s.textContent='Unable to save network details. Try again.';buttons()}};buttons();nets(autoScan)}</script></head><body onload=init()><header><h1>Leaf</h1></header><main><div class=subbar><h2>WiFi Setup</h2></div><section><p class=status id=s>Loading...</p><form id=f><label>Network</label><div class=network-row><select id=l><option value="">Select network...</option></select><button type=button id=r class=refresh title=Refresh aria-label=Refresh>&#x21bb;</button></div><input id=n autocomplete=off placeholder="Type network name"><label>Password</label><div class=row><input id=p type=password autocomplete=current-password><label class=show><input id=w type=checkbox>Show</label></div><button id=save class=hero disabled>Save and Connect</button></form></section></main></body></html>)leafhtml";
    target.send_P(200, "text/html", WIFI_SETUP_PAGE);
  }

  void sendUserStatus(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"active\":false}");
      return;
    }

    String json = "{\"active\":true,\"mode\":\"";
    json += userAppMode();
    json += "\",\"ssid\":\"";
    json += jsonEscape(user_app_using_leaf_wifi ? leafApSsid() : WiFi.SSID());
    json += "\",\"ip_address\":\"";
    json += userAppAddress();
    json += "\",\"url\":\"";
    json += userAppUrl();
    json += "\",\"firmware_version\":\"";
    json += LeafVersionInfo::firmwareVersion();
    char firmwareDisplayVersion[48];
    char hardwareDisplayVersion[24];
    LeafVersionInfo::firmwareDisplayVersion(firmwareDisplayVersion, sizeof(firmwareDisplayVersion));
    LeafVersionInfo::hardwareDisplayVersion(hardwareDisplayVersion, sizeof(hardwareDisplayVersion));
    json += "\",\"firmware_display_version\":\"";
    json += jsonEscape(firmwareDisplayVersion);
    json += "\",\"hardware_display_version\":\"";
    json += jsonEscape(hardwareDisplayVersion);
    json += "\",\"mac_address\":\"";
    json +=
        jsonEscape(settings.macAddress.isEmpty() ? settings.getMacAddress() : settings.macAddress);
    const Power::Info& power_info = power.info();
    json += "\",\"battery_percent\":";
    json += power_info.batteryPercent;
    json += ",\"battery_charging\":";
    json += power_info.charging ? "true" : "false";
    json += ",\"usb_input\":";
    json += power_info.USBinput ? "true" : "false";
    json += ",\"battery_mv\":";
    json += power_info.batteryMV;
    json += ",\"dev_mode\":";
    json += settings.dev_mode ? "true" : "false";
    json += "}";
    sendNoStoreHeaders(target);
    target.send(200, "application/json", json);
  }

  void sendDebugSessionStatus(WebServer& target) {
    String json = "{\"always_on\":";
    json += user_app_always_on ? "true" : "false";
    json += ",\"keep_bluetooth_disabled\":";
    json += user_app_keep_bluetooth_disabled ? "true" : "false";
    json += ",\"web_app_active\":";
    json += user_app_enabled ? "true" : "false";
    json += ",\"using_leaf_wifi\":";
    json += user_app_using_leaf_wifi ? "true" : "false";
    json += "}";
    sendNoStoreHeaders(target);
    target.send(200, "application/json", json);
  }

  void updateDebugSessionStatus(WebServer& target) {
    const String body = target.arg("plain");
    user_app_always_on = extractJsonBoolValue(body, "always_on", user_app_always_on);
    user_app_keep_bluetooth_disabled =
        extractJsonBoolValue(body, "keep_bluetooth_disabled", user_app_keep_bluetooth_disabled);
    sendDebugSessionStatus(target);
  }

  bool requireCommissioningHttp(WebServer& target) {
    if (commissioningHttpAllowed()) return true;

    target.send(403, "application/json",
                "{\"detail\":\"Commissioning endpoints are only available on the "
                "LeafDiagnostics network before commissioning is complete.\"}");
    return false;
  }

  void sendCommissioningMacAddress(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    String json = "{\"mac_address\":\"";
    json += settings.getMacAddress();
    json += "\"}";
    target.send(200, "application/json", json);
  }

  void sendCommissioningFirmwareVersion(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    String json = "{\"firmware_version\":\"";
    json += LeafVersionInfo::firmwareVersion();
    json += "\"}";
    target.send(200, "application/json", json);
  }

  void resetCommissioningSettings(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    const bool forceFormatSdCard =
        extractJsonBoolValue(target.arg("plain"), "force_format_sd_card", false);
    target.send(200, "application/json", "{\"reset_requested\":true}");
    delay(250);
    settings.factoryResetVario();
    settings.beginCommissioning();
    settings.setProductionTestForceFormatSdCard(forceFormatSdCard);
    delay(50);
    ESP.restart();
  }

  void saveCommissioningFanetAddress(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    String fanet_address = extractJsonStringValue(target.arg("plain"), "fanet_address");
    fanet_address.trim();
    fanet_address.toUpperCase();
    if (!isValidFanetAddress(fanet_address)) {
      target.send(400, "application/json",
                  "{\"detail\":\"fanet_address must be a 6-character hexadecimal string.\"}");
      return;
    }

    settings.fanet_address = fanet_address;
    settings.save();

    String json = "{\"fanet_address\":\"";
    json += settings.fanet_address;
    json += "\",\"saved\":true}";
    target.send(200, "application/json", json);
  }

  void startCommissioningSelfTest(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    requestInteractiveSelfTest();
    target.send(200, "application/json", selfTestSnapshotJson());
  }

  void sendCommissioningSelfTest(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    target.send(200, "application/json", selfTestSnapshotJson());
  }

  void sendCommissioningSelfTestDetails(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    sendLatestSelfTestDetails(target);
  }

  void clearCommissioningSelfTestResults(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    clearSelfTestDetailsFiles(target);
  }

  void formatCommissioningSdCard(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    if (selfTest.updateNeeded() || interactive_self_test_pending) {
      target.send(409, "application/json", "{\"detail\":\"Self test is running or pending.\"}");
      return;
    }

    if (!sdcard.isCardPresent()) {
      target.send(404, "application/json", "{\"detail\":\"SD card is not present.\"}");
      return;
    }

    if (!sdcard.format()) {
      target.send(500, "application/json", "{\"formatted\":false}");
      return;
    }

    if (!sdcard.setLabel()) {
      target.send(500, "application/json", "{\"formatted\":true,\"label_set\":false}");
      return;
    }

    target.send(200, "application/json", "{\"formatted\":true,\"label_set\":true}");
  }

  void markCommissioningComplete(WebServer& target) {
    if (!requireCommissioningHttp(target)) return;

    settings.markCommissioningComplete();
    selfTest.confirmCommissioningComplete();
    target.send(200, "application/json",
                "{\"commissioning_complete\":true,\"commissioning_pending\":false}");
  }

  void configureCommissioningRoutes() {
    if (commissioning_routes_configured) return;

    user_server.on("/mac-address", HTTP_GET, []() { sendCommissioningMacAddress(user_server); });
    user_server.on("/firmware-version", HTTP_GET,
                   []() { sendCommissioningFirmwareVersion(user_server); });
    user_server.on("/settings/factory-reset", HTTP_POST,
                   []() { resetCommissioningSettings(user_server); });
    user_server.on("/settings/fanet-address", HTTP_POST,
                   []() { saveCommissioningFanetAddress(user_server); });
    user_server.on("/self-test/interactive", HTTP_POST,
                   []() { startCommissioningSelfTest(user_server); });
    user_server.on("/self-test", HTTP_GET, []() { sendCommissioningSelfTest(user_server); });
    user_server.on("/self-test/details", HTTP_GET,
                   []() { sendCommissioningSelfTestDetails(user_server); });
    user_server.on("/self-test/results", HTTP_DELETE,
                   []() { clearCommissioningSelfTestResults(user_server); });
    user_server.on("/sd-card/format", HTTP_POST, []() { formatCommissioningSdCard(user_server); });
    user_server.on("/commissioning/complete", HTTP_POST,
                   []() { markCommissioningComplete(user_server); });

    user_server.on("/api/debug/mac-address", HTTP_GET,
                   []() { sendCommissioningMacAddress(user_server); });
    user_server.on("/api/debug/firmware-version", HTTP_GET,
                   []() { sendCommissioningFirmwareVersion(user_server); });
    user_server.on("/api/debug/settings/factory-reset", HTTP_POST,
                   []() { resetCommissioningSettings(user_server); });
    user_server.on("/api/debug/settings/fanet-address", HTTP_POST,
                   []() { saveCommissioningFanetAddress(user_server); });
    user_server.on("/api/debug/self-test/interactive", HTTP_POST,
                   []() { startCommissioningSelfTest(user_server); });
    user_server.on("/api/debug/sd-card/format", HTTP_POST,
                   []() { formatCommissioningSdCard(user_server); });
    user_server.on("/api/debug/self-test/details", HTTP_GET,
                   []() { sendCommissioningSelfTestDetails(user_server); });
    user_server.on("/api/debug/self-test/results", HTTP_DELETE,
                   []() { clearCommissioningSelfTestResults(user_server); });
    user_server.on("/api/debug/self-test", HTTP_GET,
                   []() { sendCommissioningSelfTest(user_server); });
    user_server.on("/api/debug/commissioning/complete", HTTP_POST,
                   []() { markCommissioningComplete(user_server); });

    commissioning_routes_configured = true;
  }

  void sendFirmwareUpdateStatus(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"detail\":\"Leaf Web App is not active.\"}");
      return;
    }

    heap_monitor::checkpoint("web-fw-check-start");
    try {
      String latestVersion = getLatestTagVersion();
      const bool updateAvailable =
          latestVersion != LeafVersionInfo::tagVersion() || LeafVersionInfo::otaAlwaysUpdate();
      heap_monitor::checkpoint(updateAvailable ? "web-fw-check-available" : "web-fw-check-current");

      String json = "{\"ok\":true,\"update_available\":";
      json += updateAvailable ? "true" : "false";
      json += ",\"message\":\"";
      json += updateAvailable ? "New version available! To update go to Settings>System>Update FW"
                              : "Leaf is up to date.";
      json += "\"}";
      sendNoStoreHeaders(target);
      target.send(200, "application/json", json);
    } catch (const std::runtime_error& e) {
      heap_monitor::checkpoint("web-fw-check-fail");
      Serial.printf("[WebApp] Firmware update check failed: %s\n", e.what());

      String json = "{\"ok\":false,\"detail\":\"";
      json += jsonEscape(e.what());
      json += "\"}";
      sendNoStoreHeaders(target);
      target.send(502, "application/json", json);
    }
  }

  void writeUnitSettingsJson(JsonStream& json) {
    json.write("\"units\":{\"alt_feet\":");
    json.write(settings.units_alt ? "true" : "false");
    json.write(",\"climb_fpm\":");
    json.write(settings.units_climb ? "true" : "false");
    json.write(",\"speed_mph\":");
    json.write(settings.units_speed ? "true" : "false");
    json.write(",\"distance_miles\":");
    json.write(settings.units_distance ? "true" : "false");
    json.write(",\"heading_cardinal\":");
    json.write(settings.units_heading ? "true" : "false");
    json.write(",\"temp_f\":");
    json.write(settings.units_temp ? "true" : "false");
    json.write(",\"time_12h\":");
    json.write(settings.units_hours ? "true" : "false");
    json.write("}");
  }

  void writeLogbookSummaryJson(JsonStream& json, const LogbookEntrySummary& summary) {
    json.write("{\"path\":\"");
    json.writeEscaped(summary.path.c_str());
    json.write("\",\"filename\":\"");
    json.writeEscaped(summary.filename.c_str());
    json.write("\",\"flight_id\":\"");
    json.writeEscaped(summary.flightId.c_str());
    json.write("\",\"pilot_name\":\"");
    json.writeEscaped(summary.pilotName.c_str());
    json.write("\",\"glider_display_name\":\"");
    json.writeEscaped(summary.gliderDisplayName.c_str());
    json.write("\",\"start_time_local\":\"");
    json.writeEscaped(summary.startTimeLocal.c_str());
    json.write("\",\"duration_seconds\":");
    json.writeUInt(summary.durationSeconds);
    json.write(",\"start_altitude_m\":");
    json.writeFloat(summary.startAltitudeM, 1);
    json.write(",\"end_altitude_m\":");
    json.writeFloat(summary.endAltitudeM, 1);
    json.write(",\"max_altitude_m\":");
    json.writeFloat(summary.maxAltitudeM, 1);
    json.write(",\"min_altitude_m\":");
    json.writeFloat(summary.minAltitudeM, 1);
    json.write(",\"max_altitude_above_launch_m\":");
    json.writeFloat(summary.maxAltitudeAboveLaunchM, 1);
    json.write(",\"max_climb_rate_mps\":");
    json.writeFloat(summary.maxClimbRateMps, 2);
    json.write(",\"max_sink_rate_mps\":");
    json.writeFloat(summary.maxSinkRateMps, 2);
    json.write(",\"max_ground_speed_mps\":");
    json.writeFloat(summary.maxGroundSpeedMps, 2);
    json.write(",\"path_distance_m\":");
    json.writeFloat(summary.pathDistanceM, 1);
    json.write(",\"straight_line_distance_m\":");
    json.writeFloat(summary.straightLineDistanceM, 1);
    json.write(",\"max_accel_g\":");
    json.writeFloat(summary.maxAccelG, 2);
    json.write(",\"min_accel_g\":");
    json.writeFloat(summary.minAccelG, 2);
    json.write(",\"max_temperature_c\":");
    json.writeFloat(summary.maxTemperatureC, 1);
    json.write(",\"min_temperature_c\":");
    json.writeFloat(summary.minTemperatureC, 1);
    json.write(",\"max_wind_valid\":");
    json.write(summary.maxWindValid ? "true" : "false");
    json.write(",\"max_wind_speed_mps\":");
    json.writeFloat(summary.maxWindSpeedMps, 2);
    json.write(",\"max_wind_direction_from_deg\":");
    json.writeFloat(summary.maxWindDirectionFromDeg, 0);
    json.write(",\"track_saved\":");
    json.write(summary.trackSaved ? "true" : "false");
    json.write(",\"track_path\":\"");
    json.writeEscaped(summary.trackPath.c_str());
    json.write("\"}");
  }

  void sendLogbookSummary(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"active\":false}");
      return;
    }

    const uint16_t count = LogbookStore::count();
    sendNoStoreHeaders(target);
    target.setContentLength(CONTENT_LENGTH_UNKNOWN);
    target.send(200, "application/json", "");
    char responseBuffer[1024];
    JsonStream json(target, responseBuffer, sizeof(responseBuffer));
    json.write("{\"count\":");
    json.writeUInt(count);
    json.write(",\"time_12h\":");
    json.write(settings.units_hours ? "true" : "false");
    json.write(",");
    writeUnitSettingsJson(json);
    String latestPath;
    LogbookEntrySummary latest;
    if (count > 0 && LogbookStore::newestEntryPath(latestPath) &&
        LogbookStore::readSummary(latestPath, latest)) {
      json.write(",\"latest\":");
      writeLogbookSummaryJson(json, latest);
    }

    json.write("}");
    json.finish();
  }

  void sendLogbookEntry(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"active\":false}");
      return;
    }

    heap_monitor::checkpoint("logbook-entry-start");
    String path = target.arg("path");
    if (path.isEmpty() && !LogbookStore::newestEntryPath(path)) {
      heap_monitor::checkpoint("logbook-entry-empty");
      target.send(404, "application/json",
                  "{\"ok\":false,\"error\":\"no_logs\",\"detail\":\"No logs found.\"}");
      return;
    }

    path = LogbookStore::normalizePath(path);
    if (!LogbookStore::isLogbookJsonPath(path)) {
      heap_monitor::checkpoint("logbook-entry-bad-path");
      target.send(400, "application/json",
                  "{\"ok\":false,\"error\":\"bad_path\",\"detail\":\"Invalid log path.\"}");
      return;
    }
    if (!SD_MMC.exists(path)) {
      heap_monitor::checkpoint("logbook-entry-missing");
      target.send(404, "application/json",
                  "{\"ok\":false,\"error\":\"missing\",\"detail\":\"Log file was not found.\"}");
      return;
    }

    LogbookEntrySummary summary;
    if (!LogbookStore::readSummary(path, summary)) {
      heap_monitor::checkpoint("logbook-entry-fail");
      LogbookNavigation navigation;
      LogbookStore::navigationForPath(path, navigation);

      target.setContentLength(CONTENT_LENGTH_UNKNOWN);
      target.send(422, "application/json", "");
      char responseBuffer[1024];
      JsonStream json(target, responseBuffer, sizeof(responseBuffer));
      json.write(
          "{\"ok\":false,\"error\":\"invalid_json\",\"detail\":\"Log file could not be "
          "parsed.\",\"path\":\"");
      json.writeEscaped(path.c_str());
      json.write("\",\"filename\":\"");
      const String filename = LogbookStore::filenameFromPath(path);
      json.writeEscaped(filename.c_str());
      json.write("\",\"position\":");
      json.writeUInt(navigation.position);
      json.write(",\"total\":");
      json.writeUInt(navigation.total);
      json.write(",\"previous_path\":\"");
      json.writeEscaped(navigation.previousPath.c_str());
      json.write("\",\"next_path\":\"");
      json.writeEscaped(navigation.nextPath.c_str());
      json.write("\",");
      writeUnitSettingsJson(json);
      json.write("}");
      json.finish();
      return;
    }

    LogbookNavigation navigation;
    LogbookStore::navigationForPath(summary.path, navigation);

    heap_monitor::checkpoint("logbook-entry-end");
    sendNoStoreHeaders(target);
    target.setContentLength(CONTENT_LENGTH_UNKNOWN);
    target.send(200, "application/json", "");
    char responseBuffer[1024];
    JsonStream json(target, responseBuffer, sizeof(responseBuffer));
    json.write("{\"ok\":true,\"time_12h\":");
    json.write(settings.units_hours ? "true" : "false");
    json.write(",");
    writeUnitSettingsJson(json);
    json.write(",\"position\":");
    json.writeUInt(navigation.position);
    json.write(",\"total\":");
    json.writeUInt(navigation.total);
    json.write(",\"previous_path\":\"");
    json.writeEscaped(navigation.previousPath.c_str());
    json.write("\",\"next_path\":\"");
    json.writeEscaped(navigation.nextPath.c_str());
    json.write("\",\"entry\":");
    writeLogbookSummaryJson(json, summary);
    json.write("}");
    json.finish();
  }

  void deleteLogbookEntry(WebServer& target) {
    if (!user_app_enabled) {
      target.send(404, "application/json", "{\"ok\":false,\"error\":\"inactive\"}");
      return;
    }

    heap_monitor::checkpoint("logbook-delete-start");
    String path = LogbookStore::normalizePath(target.arg("path"));
    if (!LogbookStore::isLogbookJsonPath(path)) {
      heap_monitor::checkpoint("logbook-delete-bad-path");
      target.send(400, "application/json",
                  "{\"ok\":false,\"error\":\"bad_path\",\"detail\":\"Invalid log path.\"}");
      return;
    }
    if (!SD_MMC.exists(path)) {
      heap_monitor::checkpoint("logbook-delete-missing");
      target.send(404, "application/json",
                  "{\"ok\":false,\"error\":\"missing\",\"detail\":\"Log file was not found.\"}");
      return;
    }

    LogbookNavigation navigation;
    LogbookStore::navigationForPath(path, navigation);

    if (!LogbookStore::deleteEntry(path)) {
      heap_monitor::checkpoint("logbook-delete-fail");
      target.send(
          500, "application/json",
          "{\"ok\":false,\"error\":\"delete_failed\",\"detail\":\"Log could not be deleted.\"}");
      return;
    }

    const String preferredPath =
        !navigation.previousPath.isEmpty() ? navigation.previousPath : navigation.nextPath;
    String json = "{\"ok\":true,\"next_path\":\"";
    json += jsonEscape(preferredPath);
    json += "\",\"count\":";
    json += navigation.total > 0 ? navigation.total - 1 : 0;
    json += "}";

    heap_monitor::checkpoint("logbook-delete-end");
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

    String json;
    json.reserve(WIFI_SETUP_NETWORKS_JSON_RESERVE);
    json = "{\"scanning\":false,\"networks\":[";
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
    heap_monitor::checkpoint("wifi-scan-finish");
    wifi_setup_last_scan_result = result;
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
    heap_monitor::checkpoint("wifi-scan-start");
    logWifiSetupTiming("scan-start");
    WiFi.scanDelete();
    setWifiSetupNetworksJson("{\"scanning\":true,\"networks\":[]}");
    wifi_setup_last_scan_result = WIFI_SCAN_RUNNING;
    const int16_t result = WiFi.scanNetworks(/*async=*/true, /*hidden=*/false);
    if (result == WIFI_SCAN_RUNNING) {
      wifi_setup_scan_running = true;
      heap_monitor::checkpoint("wifi-scan-running");
      return;
    }

    wifi_setup_scan_running = false;
    finishWifiNetworkScan(result);
  }

  void sendWifiNetworks(WebServer& target) {
    if (target.hasArg("refresh") && target.arg("refresh") != "0" && !wifi_setup_scan_running) {
      startWifiNetworkScan();
    }
    updateWifiNetworkScan();
    sendNoStoreHeaders(target);
    target.send(200, "application/json", wifi_setup_networks_json);
  }

  void connectToWifiFromRequest(WebServer& target) {
    heap_monitor::checkpoint("wifi-connect-request");
    const String body = target.arg("plain");
    const String ssid = extractJsonStringValue(body, "ssid");
    const String password = extractJsonStringValue(body, "password");

    if (ssid.isEmpty()) {
      target.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_ssid\"}");
      return;
    }

    if (wifi_setup_scan_running) {
      WiFi.scanDelete();
      wifi_setup_scan_running = false;
      setWifiSetupNetworksJson("{\"scanning\":false,\"networks\":[]}");
      heap_monitor::checkpoint("wifi-scan-cancel");
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false, false);
    WiFi.persistent(false);
    WiFi.begin(ssid.c_str(), password.c_str());
    heap_monitor::checkpoint("wifi-connect-begin");
    if (user_app_enabled && user_app_using_leaf_wifi) {
      user_app_provisioning = true;
    }
    wifi_setup_connecting = true;
    wifi_setup_connect_started_ms = millis();
    wifi_setup_connected_ms = 0;
    wifi_setup_connect_ssid = ssid;
    wifi_setup_connect_password = password;
    wifi_setup_connect_error = "";
    wifi_setup_connect_saved = false;
    Serial.printf("Leaf WiFi setup connecting to SSID: %s\n", ssid.c_str());
    appendWifiSetupDiagnostics("connect-request", true);
    target.send(202, "application/json", "{\"ok\":true}");
  }

  void setupUserAppServer() {
    if (user_server_started && user_server_routes_configured) return;

    if (!user_server_routes_configured) {
      user_server.on("/", HTTP_GET, []() {
        handleUserRequest("GET /", []() {
          if (diagnosticsEnabled()) user_app_route_root_count++;
          sendRedirect(user_server, user_app_provisioning ? "/wifi" : "/app");
        });
      });
      user_server.on("/app", HTTP_GET, []() {
        handleUserRequest("GET /app", []() {
          if (diagnosticsEnabled()) user_app_route_app_count++;
          sendUserAppShell(user_server);
        });
      });
      user_server.on("/wifi", HTTP_GET, []() {
        handleUserRequest("GET /wifi", []() { sendWifiSetupPage(user_server); });
      });
      user_server.on("/app/wifi", HTTP_GET, []() {
        handleUserRequest("GET /app/wifi", []() { sendWifiSetupPage(user_server); });
      });
      user_server.on("/api/user/status", HTTP_GET, []() {
        handleUserRequest("GET /api/user/status", []() {
          if (diagnosticsEnabled()) user_app_route_status_count++;
          sendUserStatus(user_server);
        });
      });
      user_server.on("/api/firmware/update-status", HTTP_POST, []() {
        handleUserRequest("POST /api/firmware/update-status",
                          []() { sendFirmwareUpdateStatus(user_server); });
      });
      user_server.on("/api/profiles", HTTP_GET, []() {
        handleUserRequest("GET /api/profiles", []() {
          if (diagnosticsEnabled()) user_app_route_profiles_get_count++;
          sendProfiles(user_server);
        });
      });
      user_server.on("/api/profiles", HTTP_PUT, []() {
        handleUserRequest("PUT /api/profiles", []() {
          if (diagnosticsEnabled()) user_app_route_profiles_put_count++;
          saveProfiles(user_server);
        });
      });
      user_server.on("/api/routes/import", HTTP_POST, []() {
        handleUserRequest("POST /api/routes/import", []() {
          if (diagnosticsEnabled()) user_app_route_routes_import_count++;
          importRoute(user_server);
        });
      });
      user_server.on("/api/routes/save", HTTP_POST, []() {
        handleUserRequest("POST /api/routes/save", []() { saveEditedRoute(user_server); });
      });
      user_server.on("/api/nav-data", HTTP_GET, []() {
        handleUserRequest("GET /api/nav-data", []() { sendNavData(user_server); });
      });
      user_server.on("/api/nav/activate-point", HTTP_POST, []() {
        handleUserRequest("POST /api/nav/activate-point", []() { activateNavPoint(user_server); });
      });
      user_server.on("/api/user-waypoints", HTTP_GET, []() {
        handleUserRequest("GET /api/user-waypoints", []() { sendUserWaypoints(user_server); });
      });
      user_server.on("/api/user-waypoints", HTTP_PUT, []() {
        handleUserRequest("PUT /api/user-waypoints", []() { saveUserWaypoints(user_server); });
      });
      user_server.on("/api/user-waypoints", HTTP_DELETE, []() {
        handleUserRequest("DELETE /api/user-waypoints", []() { deleteUserWaypoint(user_server); });
      });
      user_server.on("/api/waypoints/files", HTTP_GET, []() {
        handleUserRequest("GET /api/waypoints/files", []() { sendWaypointFileList(user_server); });
      });
      user_server.on("/api/waypoints/activate", HTTP_POST, []() {
        handleUserRequest("POST /api/waypoints/activate",
                          []() { activateWaypointFile(user_server); });
      });
      user_server.on(
          "/api/waypoints/upload", HTTP_POST,
          []() {
            handleUserRequest("POST /api/waypoints/upload", []() {
              if (diagnosticsEnabled()) user_app_route_waypoints_upload_count++;
              finishWaypointUpload(user_server);
            });
          },
          []() { receiveWaypointUpload(user_server); });
      user_server.on("/api/logbook", HTTP_GET, []() {
        handleUserRequest("GET /api/logbook", []() {
          if (diagnosticsEnabled()) user_app_route_logbook_count++;
          heap_monitor::checkpoint("logbook-summary");
          sendLogbookSummary(user_server);
        });
      });
      user_server.on("/api/logbook/entry", HTTP_GET, []() {
        handleUserRequest("GET /api/logbook/entry", []() {
          if (diagnosticsEnabled()) user_app_route_logbook_entry_count++;
          sendLogbookEntry(user_server);
        });
      });
      user_server.on("/api/logbook/entry", HTTP_DELETE, []() {
        handleUserRequest("DELETE /api/logbook/entry", []() {
          if (diagnosticsEnabled()) user_app_route_logbook_delete_count++;
          deleteLogbookEntry(user_server);
        });
      });
      user_server.on("/api/wifi/status", HTTP_GET, []() {
        handleUserRequest("GET /api/wifi/status",
                          []() { user_server.send(200, "application/json", wifiStatusJson()); });
      });
      user_server.on("/api/wifi/networks", HTTP_GET, []() {
        handleUserRequest("GET /api/wifi/networks", []() { sendWifiNetworks(user_server); });
      });
      user_server.on("/api/wifi/connect", HTTP_POST, []() {
        handleUserRequest("POST /api/wifi/connect",
                          []() { connectToWifiFromRequest(user_server); });
      });
      user_server.on("/generate_204", HTTP_GET, []() {
        handleUserRequest("GET /generate_204", []() { sendNoCaptivePortalResponse(user_server); });
      });
      user_server.on("/gen_204", HTTP_GET, []() {
        handleUserRequest("GET /gen_204", []() { sendNoCaptivePortalResponse(user_server); });
      });
      user_server.on("/hotspot-detect.html", HTTP_GET, []() {
        handleUserRequest("GET /hotspot-detect.html",
                          []() { sendNoCaptivePortalResponse(user_server, "Success"); });
      });
      user_server.on("/library/test/success.html", HTTP_GET, []() {
        handleUserRequest("GET /library/test/success.html",
                          []() { sendNoCaptivePortalResponse(user_server, "Success"); });
      });
      user_server.on("/ncsi.txt", HTTP_GET, []() {
        handleUserRequest("GET /ncsi.txt",
                          []() { sendNoCaptivePortalResponse(user_server, "Microsoft NCSI"); });
      });
      user_server.onNotFound([]() {
        handleUserRequest("NOT_FOUND", []() {
          if (diagnosticsEnabled()) user_app_route_not_found_count++;
          if (handleCaptivePortalRequest(user_server)) return;
          user_server.send(404, "text/plain", "Not found");
        });
      });
      user_server_routes_configured = true;
    }

    webserver_setup();
    if (!user_server_started) {
      user_server.begin();
      user_server_started = true;
    }
    Serial.printf("Leaf Web App started: %s\n", userAppUrl().c_str());
  }
}  // namespace

constexpr auto endl = "\n";

void writeScreenshotBuffer(const char* buffer) {
  Serial.println("Writing screenshot buffer");
  send_buffer += buffer;
}

void webserver_setup() {
  configureCommissioningRoutes();
  if (!user_server_started && commissioningHttpAllowed()) {
    user_server.begin();
    user_server_started = true;
    Serial.println("Leaf commissioning webserver started");
  }

  if (!settings.dev_mode) return;
  if (debug_routes_configured) return;

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

        <canvas id="screenshotCanvas" style="border:0px solid #000; image-rendering: pixelated;"></canvas>

        <script>
        function drawXBM(xbmData, canvasId) {
          const canvas = document.getElementById(canvasId);
          const ctx = canvas.getContext('2d');

          // Extract width and height (using regex, adjust as needed)
          const widthMatch = xbmData.match(/width (\d+)/);
          const heightMatch = xbmData.match(/height (\d+)/);
          const width = parseInt(widthMatch[1]);
          const height = parseInt(heightMatch[1]);
          canvas.width = height;
          canvas.height = width;
          canvas.style.width = (height * 2) + 'px';
          canvas.style.height = (width * 2) + 'px';

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

  user_server.on("/api/debug/session", HTTP_GET, []() { sendDebugSessionStatus(user_server); });

  user_server.on("/api/debug/session", HTTP_POST, []() { updateDebugSessionStatus(user_server); });

  user_server.on("/app/debug/memory", HTTP_GET,
                 []() { user_server.send(200, "text/plain", getMemoryUsage()); });

  user_server.on("/api/debug/discovery", HTTP_GET, []() {
    factoryDiscovery.update();
    user_server.send(200, "application/json", factoryDiscovery.statusJson());
  });

  debug_routes_configured = true;
}

void webserver_loop() {
  if (WiFi.status() == WL_CONNECTED || user_app_enabled) {
    if (user_server_started) {
      user_server.handleClient();
    }

    if (user_app_enabled) {
      if (diagnosticsEnabled()) user_app_loop_count++;
      updateUserAppStationPeak();
      if (user_app_provisioning) updateWifiNetworkScan();
      if (user_app_dns_started) dns_server.processNextRequest();
      if (diagnosticsEnabled()) user_app_handle_count++;
      if (user_app_provisioning) appendWifiSetupDiagnostics("loop");
      if (webserver_wifi_setup_ready_to_finish()) {
        appendWifiSetupDiagnostics("transition-start", true);
        Serial.printf("Leaf WiFi setup connected to %s; closing setup portal\n",
                      WiFi.SSID().c_str());
        webserver_disable_user_app();
        appendWifiSetupDiagnostics("transition-finished", true);
        display.update();
      }
    }
    updatePendingInteractiveSelfTest();
  }
}

void webserver_enable_user_app(bool useLeafWifi) {
  heap_monitor::clear();
  resetUserAppCounters();
  heap_monitor::checkpoint("enable-start");
  pauseServicesForUserApp();
  heap_monitor::checkpoint("after-pause-services");
  if (useLeafWifi || WiFi.status() != WL_CONNECTED) {
    leaf_wifi::prepareForLeafAccessPoint();
    heap_monitor::checkpoint("after-prepare-ap");
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    startLeafAp();
    startLeafApDns();
    heap_monitor::checkpoint("after-softap");
    user_app_using_leaf_wifi = true;
  } else {
    user_app_using_leaf_wifi = false;
    heap_monitor::checkpoint("using-sta");
  }

  user_app_enabled = true;
  user_app_provisioning = false;
  setupUserAppServer();
  heap_monitor::checkpoint("after-user-server");
  webserver_setup();
  heap_monitor::checkpoint("enabled");
  appendWifiSetupDiagnostics(useLeafWifi ? "enable-user-app-ap" : "enable-user-app-network", true);
}

void webserver_enable_wifi_setup() {
  heap_monitor::clear();
  heap_monitor::checkpoint("wifi-setup-start");
  pauseServicesForUserApp();

  if (diagnosticsEnabled()) {
    wifi_setup_cycle++;
    wifi_setup_started_ms = millis();
  }
  logWifiSetupTiming("start");
  leaf_wifi::prepareForUserWifiSetup();
  heap_monitor::checkpoint("wifi-setup-prepared");
  logWifiSetupTiming("after-radio-prepare");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  startLeafAp();
  heap_monitor::checkpoint("wifi-setup-ap");
  logWifiSetupTiming("after-softAP");
  user_app_enabled = true;
  user_app_using_leaf_wifi = true;
  user_app_provisioning = true;
  setupUserAppServer();
  heap_monitor::checkpoint("wifi-setup-server");
  logWifiSetupTiming("after-user-server");
  startLeafApDns();
  heap_monitor::checkpoint("wifi-setup-dns");
  logWifiSetupTiming("after-dns");
  webserver_setup();
  logWifiSetupTiming("after-main-server");
  startWifiNetworkScan();
  logWifiSetupTiming("after-scan-start");
  appendWifiSetupDiagnostics("setup-started", true);
  Serial.printf("Leaf WiFi setup started: http://%s/wifi\n", WiFi.softAPIP().toString().c_str());
}

void webserver_disable_user_app() {
  appendWifiSetupDiagnostics("disable-start", true);
  heap_monitor::checkpoint("disable-start");
  dumpUserAppCounters("disable-start");
  user_app_enabled = false;
  stopLeafApDns();
  user_app_provisioning = false;
  if (wifi_setup_scan_running) WiFi.scanDelete();
  wifi_setup_scan_running = false;
  releaseWifiSetupNetworksJson();
  wifi_setup_connecting = false;
  wifi_setup_connected_ms = 0;
  wifi_setup_connect_ssid = "";
  wifi_setup_connect_password = "";
  wifi_setup_connect_error = "";
  wifi_setup_connect_saved = false;
  if (user_server_started) {
    user_server.stop();
    user_server_started = false;
    heap_monitor::checkpoint("after-user-stop");
  }
  if (user_app_using_leaf_wifi) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    heap_monitor::checkpoint("after-ap-stop");
  }
  user_app_using_leaf_wifi = false;
  heap_monitor::checkpoint("disabled");
  dumpUserAppCounters("disabled");
  appendWifiSetupDiagnostics("portal-disabled", true);
  heap_monitor::dumpToSd();
  resumeServicesAfterUserApp();
  heap_monitor::checkpoint("after-resume-services");
  appendWifiSetupDiagnostics("services-resumed", true);
}

bool webserver_user_app_active() { return user_app_enabled; }

bool webserver_user_app_always_on() { return user_app_always_on; }

bool webserver_wifi_setup_active() { return user_app_enabled && user_app_provisioning; }

bool webserver_wifi_setup_ready_to_finish() {
  if (!user_app_enabled || !user_app_provisioning) return false;
  if (!stationConnectionReady()) return false;

  if (wifi_setup_connecting) {
    markWifiSetupConnected("transition-connected");
  }
  if (wifi_setup_connected_ms == 0) markWifiSetupConnected("transition-ready");
  return millis() - wifi_setup_connected_ms >= WIFI_SETUP_AP_SUCCESS_GRACE_MS;
}

bool webserver_user_app_using_leaf_wifi() { return user_app_enabled && user_app_using_leaf_wifi; }

String webserver_user_app_url() { return userAppUrl(); }

String webserver_leaf_ap_ssid() { return leafApSsid(); }

String webserver_leaf_ap_password() { return leafApPassword(); }

String webserver_leaf_ap_wifi_qr() { return leafApWifiQr(); }
