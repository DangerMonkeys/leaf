// WiFi stand-in.
//
// The emulator reports "not connected", which is the state the firmware is in for almost all of a
// flight.  Screens that show connection state, and the code paths gated on WL_CONNECTED, behave
// as they do on a device out of range.
#pragma once

#include <stdint.h>

#include "IPAddress.h"
#include "WString.h"

typedef enum {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6
} wl_status_t;

typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
#define WIFI_MODE_STA WIFI_STA
#define WIFI_MODE_AP WIFI_AP

typedef int32_t WiFiEvent_t;
typedef struct {
  int unused;
} WiFiEventInfo_t;

#define ARDUINO_EVENT_WIFI_STA_CONNECTED 4
#define ARDUINO_EVENT_WIFI_STA_DISCONNECTED 5
#define ARDUINO_EVENT_WIFI_STA_GOT_IP 7

class WiFiClass {
 public:
  wl_status_t status() { return WL_DISCONNECTED; }
  bool begin() { return false; }
  bool begin(const char* ssid, const char* password = nullptr) { return false; }
  bool mode(wifi_mode_t mode) { return true; }
  wifi_mode_t getMode() { return WIFI_OFF; }
  bool disconnect(bool wifiOff = false, bool eraseAp = false) { return true; }
  bool softAP(const char* ssid, const char* password = nullptr) { return false; }
  bool softAPdisconnect(bool wifiOff = false) { return true; }
  IPAddress localIP() { return IPAddress(0, 0, 0, 0); }
  IPAddress softAPIP() { return IPAddress(0, 0, 0, 0); }
  String SSID() { return String(); }
  String macAddress() { return String("00:00:00:00:00:00"); }
  int32_t RSSI() { return 0; }
  int scanNetworks(bool async = false) { return 0; }
  String SSID(int index) { return String(); }
  int32_t RSSI(int index) { return 0; }
  uint8_t encryptionType(int index) { return 0; }
  void scanDelete() {}
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void setSleep(bool) {}
  void setHostname(const char*) {}
  template <typename T>
  void onEvent(T callback) {}
  template <typename T>
  void onEvent(T callback, WiFiEvent_t event) {}
};

extern WiFiClass WiFi;
