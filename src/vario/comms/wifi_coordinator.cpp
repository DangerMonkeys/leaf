#include "comms/wifi_coordinator.h"

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <cstring>

#include "ui/settings/settings.h"

namespace leaf_wifi {
  namespace {
    bool diagnostics_disabled_until_reboot = false;
    enum class SavedNetworkConnectStage : uint8_t {
      IDLE,
      CONNECTING,
    };

    struct SavedNetwork {
      String ssid;
      String password;
    };

    struct SavedNetworkList {
      SavedNetwork items[3];
      uint8_t count = 0;
    };

    SavedNetworkConnectStage saved_network_stage = SavedNetworkConnectStage::IDLE;
    uint32_t saved_network_connect_started_ms = 0;
    uint8_t saved_network_connect_index = 0;
    uint8_t saved_network_count = 0;
    uint32_t saved_network_cycle = 0;
    String saved_network_fallback_ssid = "";
    String saved_network_fallback_password = "";
    constexpr uint32_t WIFI_SETTLE_MS = 150;
    constexpr uint32_t WIFI_HARD_RESET_SETTLE_MS = 500;
    constexpr uint32_t SAVED_NETWORK_CONNECT_MS = 5000;
    constexpr uint8_t SAVED_NETWORK_SCHEMA_VERSION = 1;
    constexpr const char* WIFI_CREDENTIALS_NAMESPACE = "leafWifi";
    constexpr const char* DIAGNOSTICS_DIR = "/diagnostics";
    constexpr const char* WIFI_AUTOCONNECT_DIAGNOSTICS_FILE = "/diagnostics/wifi_autoconnect.csv";

    void settleWifi() { delay(WIFI_SETTLE_MS); }

    void hardSettleWifi() { delay(WIFI_HARD_RESET_SETTLE_MS); }

    bool diagnosticsEnabled() { return settings.dev_mode; }

    void printCsvString(File& file, const String& value) {
      file.print('"');
      for (size_t i = 0; i < value.length(); i++) {
        if (value[i] == '"') file.print('"');
        file.print(value[i]);
      }
      file.print('"');
    }

    void appendAutoConnectDiagnostics(const char* event, const String& target_ssid = String(),
                                      uint8_t saved_count = saved_network_count) {
      if (!diagnosticsEnabled()) return;
      if (!SD_MMC.exists(DIAGNOSTICS_DIR)) SD_MMC.mkdir(DIAGNOSTICS_DIR);

      const bool existed = SD_MMC.exists(WIFI_AUTOCONNECT_DIAGNOSTICS_FILE);
      File file = SD_MMC.open(WIFI_AUTOCONNECT_DIAGNOSTICS_FILE, "a", true);
      if (!file) return;

      if (!existed || file.size() == 0) {
        file.println(
            "millis,event,cycle,attempt_index,saved_count,elapsed_ms,target_ssid,current_ssid,"
            "wifi_status,wifi_mode,rssi,local_ip,diagnostics_allowed,free_heap,max_alloc_heap,"
            "current_task");
      }

      const uint32_t now = millis();
      const uint32_t elapsed =
          saved_network_connect_started_ms == 0 ? 0 : now - saved_network_connect_started_ms;
      const int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;

      file.printf("%lu,", static_cast<unsigned long>(now));
      printCsvString(file, event ? String(event) : String(""));
      file.printf(",%lu,%u,%u,%lu,", static_cast<unsigned long>(saved_network_cycle),
                  static_cast<unsigned int>(saved_network_connect_index),
                  static_cast<unsigned int>(saved_count), static_cast<unsigned long>(elapsed));
      printCsvString(file, target_ssid);
      file.print(',');
      printCsvString(file, WiFi.SSID());
      file.printf(",%d,%d,%d,", static_cast<int>(WiFi.status()), static_cast<int>(WiFi.getMode()),
                  rssi);
      printCsvString(file, WiFi.localIP().toString());
      file.printf(",%u,%lu,%lu,", diagnosticsAllowed() ? 1 : 0,
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      printCsvString(file, pcTaskGetName(NULL));
      file.println();
      file.close();
    }

    void disableDiagnostics() {
      if (!diagnostics_disabled_until_reboot) {
        Serial.println("WiFi coordinator: diagnostic network disabled until reboot");
      }
      diagnostics_disabled_until_reboot = true;
    }

    void clearSavedNetworkAttempt() {
      saved_network_stage = SavedNetworkConnectStage::IDLE;
      saved_network_connect_started_ms = 0;
      saved_network_connect_index = 0;
      saved_network_count = 0;
      saved_network_fallback_ssid = "";
      saved_network_fallback_password = "";
    }

    String fixedWifiFieldToString(const uint8_t* field, size_t field_size) {
      char buffer[65] = {};
      const size_t copy_len = field_size < sizeof(buffer) - 1 ? field_size : sizeof(buffer) - 1;
      memcpy(buffer, field, copy_len);
      buffer[copy_len] = '\0';
      return String(buffer);
    }

    bool readActiveStationConfig(SavedNetwork& network) {
      wifi_config_t config = {};
      if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK) return false;
      network.ssid = fixedWifiFieldToString(config.sta.ssid, sizeof(config.sta.ssid));
      network.password = fixedWifiFieldToString(config.sta.password, sizeof(config.sta.password));
      return !network.ssid.isEmpty();
    }

    void storeSavedNetworkList(const SavedNetworkList& networks) {
      Preferences prefs;
      if (!prefs.begin(WIFI_CREDENTIALS_NAMESPACE, false)) {
        Serial.println("WiFi coordinator: failed to open saved network store");
        return;
      }

      prefs.clear();
      prefs.putUChar("ver", SAVED_NETWORK_SCHEMA_VERSION);
      prefs.putUChar("count", networks.count);
      for (uint8_t i = 0; i < networks.count; i++) {
        char ssid_key[8];
        char pass_key[8];
        snprintf(ssid_key, sizeof(ssid_key), "ssid%u", i);
        snprintf(pass_key, sizeof(pass_key), "pass%u", i);
        prefs.putString(ssid_key, networks.items[i].ssid);
        prefs.putString(pass_key, networks.items[i].password);
      }
      prefs.end();
    }

    SavedNetworkList loadSavedNetworkList(bool migrate_legacy_station = true) {
      SavedNetworkList networks;

      Preferences prefs;
      if (prefs.begin(WIFI_CREDENTIALS_NAMESPACE, true)) {
        const uint8_t version = prefs.getUChar("ver", 0);
        const uint8_t count = prefs.getUChar("count", 0);
        if (version == SAVED_NETWORK_SCHEMA_VERSION) {
          for (uint8_t i = 0; i < count && networks.count < 3; i++) {
            char ssid_key[8];
            char pass_key[8];
            snprintf(ssid_key, sizeof(ssid_key), "ssid%u", i);
            snprintf(pass_key, sizeof(pass_key), "pass%u", i);
            SavedNetwork network;
            network.ssid = prefs.getString(ssid_key, "");
            network.password = prefs.getString(pass_key, "");
            if (!network.ssid.isEmpty()) {
              networks.items[networks.count++] = network;
            }
          }
        }
        prefs.end();
      }

      if (networks.count == 0 && migrate_legacy_station) {
        SavedNetwork legacy_network;
        if (readActiveStationConfig(legacy_network)) {
          networks.items[0] = legacy_network;
          networks.count = 1;
          storeSavedNetworkList(networks);
          Serial.printf("WiFi coordinator: migrated saved network '%s'\n",
                        legacy_network.ssid.c_str());
        }
      }

      return networks;
    }

    bool hasSavedStationConfig() {
      wifi_config_t config = {};
      if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK) return false;
      return config.sta.ssid[0] != '\0';
    }

    bool writeActiveStationConfig(const String& ssid, const String& password) {
      if (ssid.isEmpty()) return false;

      wifi_config_t config = {};
      strlcpy(reinterpret_cast<char*>(config.sta.ssid), ssid.c_str(), sizeof(config.sta.ssid));
      strlcpy(reinterpret_cast<char*>(config.sta.password), password.c_str(),
              sizeof(config.sta.password));

      WiFi.persistent(true);
      const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
      WiFi.persistent(false);
      if (err != ESP_OK) {
        Serial.printf("WiFi coordinator: failed to save active station config: %d\n", err);
        return false;
      }
      return true;
    }

    void clearActiveStationConfig() {
      clearSavedNetworkAttempt();
      WiFi.scanDelete();
      WiFi.disconnect(/*wifioff=*/false, /*eraseCredentials=*/false);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);

      wifi_config_t config = {};
      WiFi.persistent(true);
      const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
      WiFi.persistent(false);
      if (err != ESP_OK) {
        Serial.printf("WiFi coordinator: failed to clear active station config: %d\n", err);
      }
    }

    void connectToSavedNetwork(const SavedNetwork& network, uint8_t index, uint8_t count) {
      WiFi.disconnect(/*wifioff=*/false, /*eraseCredentials=*/false);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      if (index == 0) {
        writeActiveStationConfig(network.ssid, network.password);
        WiFi.begin();
      } else {
        WiFi.persistent(false);
        WiFi.begin(network.ssid.c_str(), network.password.c_str());
      }
      saved_network_stage = SavedNetworkConnectStage::CONNECTING;
      saved_network_connect_started_ms = millis();
      saved_network_connect_index = index;
      saved_network_count = count;
      saved_network_fallback_ssid = network.ssid;
      saved_network_fallback_password = network.password;
      Serial.printf("WiFi coordinator: trying saved network %u '%s'\n", index,
                    network.ssid.c_str());
      if (diagnosticsEnabled())
        appendAutoConnectDiagnostics("attempt-network", network.ssid, count);
    }

    void tryNextSavedNetwork() {
      const SavedNetworkList networks = loadSavedNetworkList();
      saved_network_count = networks.count;
      const uint8_t next_index = saved_network_connect_index + 1;
      if (next_index >= networks.count) {
        Serial.println("WiFi coordinator: no more saved networks to try");
        if (diagnosticsEnabled()) {
          appendAutoConnectDiagnostics("attempt-exhausted", saved_network_fallback_ssid,
                                       networks.count);
        }
        clearSavedNetworkAttempt();
        return;
      }

      if (diagnosticsEnabled()) {
        appendAutoConnectDiagnostics("attempt-fallback", networks.items[next_index].ssid,
                                     networks.count);
      }
      connectToSavedNetwork(networks.items[next_index], next_index, networks.count);
    }

    void updateSavedNetworkAttempt() {
      if (saved_network_stage == SavedNetworkConnectStage::IDLE) return;

      if (WiFi.status() == WL_CONNECTED) {
        if (diagnosticsEnabled()) {
          appendAutoConnectDiagnostics("attempt-connected", saved_network_fallback_ssid);
        }
        if (saved_network_connect_index > 0 && !saved_network_fallback_ssid.isEmpty()) {
          rememberSuccessfulNetwork(saved_network_fallback_ssid, saved_network_fallback_password);
        }
        clearSavedNetworkAttempt();
        return;
      }

      const uint32_t elapsed = millis() - saved_network_connect_started_ms;
      switch (saved_network_stage) {
        case SavedNetworkConnectStage::CONNECTING:
          if (elapsed > SAVED_NETWORK_CONNECT_MS) {
            Serial.printf("WiFi coordinator: saved network '%s' did not connect\n",
                          saved_network_fallback_ssid.c_str());
            if (diagnosticsEnabled()) {
              appendAutoConnectDiagnostics("attempt-timeout", saved_network_fallback_ssid);
            }
            tryNextSavedNetwork();
          }
          break;
        case SavedNetworkConnectStage::IDLE:
        default:
          break;
      }
    }

    void stopScansAndSta(bool eraseCredentials) {
      clearSavedNetworkAttempt();
      WiFi.scanDelete();
      WiFi.disconnect(/*wifioff=*/false, eraseCredentials);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      settleWifi();
      WiFi.scanDelete();
    }

    void resetRadioForSetup(bool eraseCredentials) {
      clearSavedNetworkAttempt();
      WiFi.scanDelete();
      WiFi.softAPdisconnect(true);
      WiFi.disconnect(/*wifioff=*/false, eraseCredentials);
      WiFi.mode(WIFI_OFF);
      hardSettleWifi();
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      hardSettleWifi();
      WiFi.scanDelete();
    }
  }  // namespace

  void disableDiagnosticsUntilReboot() { disableDiagnostics(); }

  bool diagnosticsAllowed() { return !diagnostics_disabled_until_reboot; }

  void prepareForUserWifiSetup() {
    disableDiagnostics();
    resetRadioForSetup(/*eraseCredentials=*/false);
  }

  void prepareForUserWifiSetupFast() {
    disableDiagnostics();
    clearSavedNetworkAttempt();
    WiFi.scanDelete();
    WiFi.disconnect(/*wifioff=*/false, /*eraseCredentials=*/false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
  }

  void prepareForLeafAccessPoint() {
    disableDiagnostics();
    stopScansAndSta(/*eraseCredentials=*/false);
  }

  void resetUserWifiSettings() {
    disableDiagnostics();
    clearSavedNetworkCredentials();
    resetRadioForSetup(/*eraseCredentials=*/false);
    Serial.println("WiFi settings reset");
  }

  void clearSavedNetworkCredentials() {
    clearSavedNetworkAttempt();

    Preferences prefs;
    if (prefs.begin(WIFI_CREDENTIALS_NAMESPACE, false)) {
      prefs.clear();
      prefs.end();
    }

    clearActiveStationConfig();
  }

  void rememberSuccessfulNetwork(const String& ssid, const String& password) {
    if (ssid.isEmpty()) return;

    const SavedNetworkList existing = loadSavedNetworkList();
    SavedNetworkList updated;
    updated.items[updated.count++] = SavedNetwork{ssid, password};

    for (uint8_t i = 0; i < existing.count && updated.count < 3; i++) {
      if (existing.items[i].ssid == ssid) continue;
      updated.items[updated.count++] = existing.items[i];
    }

    storeSavedNetworkList(updated);
    writeActiveStationConfig(ssid, password);
    Serial.printf("WiFi coordinator: saved network '%s' as default (%u stored)\n", ssid.c_str(),
                  updated.count);
  }

  void attemptSavedNetworkConnection() {
    disableDiagnostics();
    clearSavedNetworkAttempt();
    saved_network_cycle++;
    if (diagnosticsEnabled()) appendAutoConnectDiagnostics("attempt-start");
    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    const SavedNetworkList networks = loadSavedNetworkList();
    saved_network_count = networks.count;
    if (networks.count > 0) {
      connectToSavedNetwork(networks.items[0], 0, networks.count);
    } else if (hasSavedStationConfig()) {
      WiFi.begin();
      saved_network_stage = SavedNetworkConnectStage::CONNECTING;
      saved_network_connect_started_ms = millis();
      saved_network_count = 1;
      if (diagnosticsEnabled()) appendAutoConnectDiagnostics("attempt-active-config", "", 1);
    } else {
      if (diagnosticsEnabled()) appendAutoConnectDiagnostics("attempt-no-saved", "", 0);
      clearSavedNetworkAttempt();
      return;
    }
  }

  void disconnectFromNetwork() {
    if (diagnosticsEnabled()) appendAutoConnectDiagnostics("disconnect");
    clearSavedNetworkAttempt();
    WiFi.scanDelete();
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
  }

  bool savedNetworkConnectionInProgress() {
    updateSavedNetworkAttempt();
    return saved_network_stage != SavedNetworkConnectStage::IDLE;
  }

}  // namespace leaf_wifi
