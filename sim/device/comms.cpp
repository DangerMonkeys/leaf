// Radios and network services the emulator does not model.
//
// Bluetooth, FANET/LoRa, WiFi provisioning, the configuration webserver, OTA and the factory
// discovery responder all need hardware or a network stack that has no meaning on the host.  Each
// keeps its interface so the firmware's call sites and menu screens compile and behave sensibly
// (features report themselves as unavailable), without pretending to work.

#include <Arduino.h>

#include "hardware/configuration.h"

#include "comms/ble.h"
#include "comms/factory_discovery.h"
#include "comms/ota.h"
#include "comms/sd_firmware_update.h"
#include "comms/webserver.h"
#include "comms/wifi_coordinator.h"
#include "diagnostics/diagnostic_network/diagnostic_network.h"

#ifdef FANET_CAPABLE
#include "comms/fanet_radio.h"
#endif

// ---------------------------------------------------------------- Bluetooth

BLE& BLE::get() {
  static BLE instance;
  return instance;
}

void BLE::setup() { Serial.println("BLE: not emulated"); }
void BLE::start() { started = true; }
void BLE::stop() { started = false; }
void BLE::end() { started = false; }
void BLE::on_receive(const GpsMessage& msg) {}
void BLE::on_receive(const FanetPacket& msg) {}
void BLE::sendVarioUpdate() {}
void BLE::sendGpsUpdate(TinyGPSPlus& gps) {}
void BLE::sendFanetUpdate(FanetPacket& packetMsg) {}
void BLE::addChecksumToNMEA(etl::istring& nmea) {}
void BLE::bleTask(void*) {}
void BLE::timerCallback(TimerHandle_t timer) {}

// ---------------------------------------------------------------- FANET / LoRa

#ifdef FANET_CAPABLE
FanetRadio fanetRadio;

String FanetAddressToString(FANET::Address address) { return String("000:000"); }

void FanetRadio::setup() {}
void FanetRadio::subscribe(etl::imessage_bus* bus) {}
void FanetRadio::begin(const FanetRadioRegion& region) { state = FanetRadioState::UNINSTALLED; }
void FanetRadio::end() { state = FanetRadioState::UNINITIALIZED; }
void FanetRadio::logDetectionResult() { Serial.println("FANET: radio not emulated"); }
FanetRadioState FanetRadio::getState() { return state; }
void FanetRadio::setGroundTrackingMode(const FANET::GroundTrackingPayload::TrackingType& mode) {}
void FanetRadio::setCurrentLocation(const float& lat, const float& lon, const uint32_t& alt,
                                    const int& heading, const float& climbRate,
                                    const float& speedKmh) {}
const FANET::Protocol::Stats FanetRadio::getStats() const { return FANET::Protocol::Stats(); }
String FanetRadio::getAddress() { return String("000:000"); }
const FanetNeighbors::NeighborMap& FanetRadio::getNeighborTable() const { return neighbors.get(); }
void FanetRadio::on_receive(const GpsReading& msg) {}
bool FanetRadio::fanet_sendFrame(uint8_t codingRate, etl::span<const uint8_t> data) {
  return false;
}
#endif

// ---------------------------------------------------------------- WiFi provisioning

namespace leaf_wifi {
  void disableDiagnosticsUntilReboot() {}
  bool diagnosticsAllowed() { return false; }
  void prepareForUserWifiSetup() {}
  void prepareForUserWifiSetupFast() {}
  void prepareForLeafAccessPoint() {}
  void resetUserWifiSettings() {}
  void clearSavedNetworkCredentials() {}
  void rememberSuccessfulNetwork(const String& ssid, const String& password) {}
  void attemptSavedNetworkConnection() {}
  void disconnectFromNetwork() {}
  bool savedNetworkConnectionInProgress() { return false; }
}  // namespace leaf_wifi

// ---------------------------------------------------------------- webserver

void webserver_setup() {}
void webserver_loop() {}
void webserver_enable_user_app(bool useLeafWifi) {}
void webserver_enable_wifi_setup() {}
void webserver_disable_user_app() {}
bool webserver_user_app_active() { return false; }
bool webserver_user_app_always_on() { return false; }
bool webserver_wifi_setup_active() { return false; }
bool webserver_wifi_setup_ready_to_finish() { return false; }
bool webserver_user_app_using_leaf_wifi() { return false; }
String webserver_user_app_url() { return String(); }
String webserver_leaf_ap_ssid() { return String(); }
String webserver_leaf_ap_password() { return String(); }
String webserver_leaf_ap_wifi_qr() { return String(); }

// ---------------------------------------------------------------- OTA and SD firmware update

String getLatestTagVersion() { return String(); }
bool otaUpdateAvailable(const String& latestTagVersion) { return false; }
void PerformOTAUpdate(const char* tag) { Serial.println("OTA: not available in the emulator"); }

namespace sd_firmware_update {
  void handleBootUpdate() {}
}  // namespace sd_firmware_update

// ---------------------------------------------------------------- factory discovery

FactoryDiscovery factoryDiscovery;

void FactoryDiscovery::update() {}
String FactoryDiscovery::statusJson() const { return String("{\"emulated\":true}"); }
void FactoryDiscovery::start() {}
void FactoryDiscovery::stop() {}
void FactoryDiscovery::onPacket(AsyncUDPPacket& packet) {}

// ---------------------------------------------------------------- diagnostic network

DiagnosticNetwork diagnostic_network;

bool DiagnosticNetwork::connected() const { return false; }
void DiagnosticNetwork::update() {}
void DiagnosticNetwork::reset(const char* reason) { state_ = State::Ready; }
bool DiagnosticNetwork::canSleepWhileCharging() const { return true; }
bool DiagnosticNetwork::shouldResetWhenSwitchingOn() const { return false; }
void DiagnosticNetwork::onUnexpectedState(const char* action, State actual) const {}
void DiagnosticNetwork::maybeLookForNetwork() {}
void DiagnosticNetwork::checkForDiagnosticNetwork() {}
void DiagnosticNetwork::checkForConnection() {}
