#include "comms/ble.h"

#include <Arduino.h>

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include "TinyGPSPlus.h"
#include "comms/fanet_radio.h"
#include "etl/string.h"
#include "etl/string_stream.h"
#include "etl/variant.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/settings/settings.h"
#include "utils/lock_guard.h"

// These UUIDs are for BLE UART services and characteristics.
// This is required to be UART due to a requirement for
// compatibility with SeeYou Navigator.
#define LEAF_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // SPP service
#define LEAF_LK8EX1_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // SPP characteristic (TX/RX)

// Custom settings service — allows a Web Bluetooth client to read and write all device settings.
#define LEAF_SETTINGS_SVC_UUID  "0A29AB06-0F01-4B27-AE93-1E86D81B9784"
#define LEAF_SETTINGS_CMD_UUID  "42433AC8-814C-4E2F-AD52-C8F6CBCC96D7"  // WRITE  client→device
#define LEAF_SETTINGS_RSP_UUID  "A621D682-D621-43D5-A1BD-4AF1F3F5271F"  // NOTIFY device→client

// ATT indication payload: MTU(512) − 3 header bytes − 1 flag prefix byte.
static constexpr size_t BLE_CHUNK_PAYLOAD = 508;

void BLE::beginChunkedSend(String data) {
  Serial.printf("[BLE] beginChunkedSend: %u bytes\n", (unsigned)data.length());
  pending_data = std::move(data);
  pending_offset = 0;
  sendNextChunk();
}

void BLE::sendNextChunk() {
  if (pending_data.isEmpty()) {
    Serial.println("[BLE] sendNextChunk: no pending transfer");
    return;
  }
  size_t total = pending_data.length();
  size_t len = min(BLE_CHUNK_PAYLOAD, total - pending_offset);
  bool last = (pending_offset + len >= total);

  uint8_t buf[BLE_CHUNK_PAYLOAD + 1];
  buf[0] = last ? 0xFF : 0x00;
  memcpy(buf + 1, pending_data.c_str() + pending_offset, len);

  Serial.printf("[BLE] sendNextChunk: offset=%u len=%u last=%d\n",
                (unsigned)pending_offset, (unsigned)len, last ? 1 : 0);
  pSettingsRspChar->setValue(buf, len + 1);
  pSettingsRspChar->notify();

  pending_offset += len;
  if (last) {
    pending_data = String();
    pending_offset = 0;
    Serial.println("[BLE] sendNextChunk: transfer complete");
  }
}

// Handles JSON commands written by a Web Bluetooth client to the CMD characteristic.
class SettingsCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    NimBLECharacteristic* rsp = NimBLEDevice::getServer()
                                    ->getServiceByUUID(LEAF_SETTINGS_SVC_UUID)
                                    ->getCharacteristic(LEAF_SETTINGS_RSP_UUID);

    auto sendError = [&](const char* msg) {
      Serial.printf("[BLE] settings error: %s\n", msg);
      String resp = String("{\"ok\":false,\"error\":\"") + msg + "\"}";
      rsp->setValue((const uint8_t*)resp.c_str(), resp.length());
      rsp->notify();
    };

    // Ignore commands when the settings page is not open
    if (!BLE::get().isSettingsServiceActive()) {
      sendError("settings service not active");
      return;
    }

    NimBLEAttValue raw = pChar->getValue();
    Serial.printf("[BLE] settings onWrite: %u bytes received\n", (unsigned)raw.length());
    if (raw.length() == 0) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, raw.data(), raw.length());
    if (err) {
      Serial.printf("[BLE] JSON parse error: %s\n", err.c_str());
      sendError("invalid json");
      return;
    }

    const char* op = doc["op"] | "";
    Serial.printf("[BLE] settings op: %s\n", op);

    if (strcmp(op, "get") == 0) {
      String json = settings.toJson();
      Serial.printf("[BLE] settings get: %u bytes\n", (unsigned)json.length());
      BLE::get().beginChunkedSend(std::move(json));
      return;
    }

    if (strcmp(op, "ack") == 0) {
      Serial.println("[BLE] settings ack");
      BLE::get().sendNextChunk();
      return;
    }

    if (strcmp(op, "apply") == 0) {
      // Require PIN for writes
      uint32_t provided_pin = doc["pin"] | 0;
      if (provided_pin == 0 || provided_pin != BLE::get().getSettingsPin()) {
        sendError("invalid pin");
        return;
      }

      if (!settings.applyFromJson(doc["settings"])) {
        sendError("invalid settings");
        return;
      }

      settings.boot_toOnState = true;
      settings.save();
      Serial.println("[BLE] settings apply: success, restarting");

      const char* resp = "{\"ok\":true}";
      rsp->setValue((const uint8_t*)resp, strlen(resp));
      rsp->notify();

      speaker.playSound(fx::confirm);
      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP.restart();
      return;
    }

    sendError("unknown op");
  }

  void onRead(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    Serial.printf("[BLE] CMD read (unexpected)  handle=%d\n", connInfo.getConnHandle());
  }

  void onStatus(NimBLECharacteristic* pChar, int code) override {
    Serial.printf("[BLE] CMD status  code=%d\n", code);
  }

  void onSubscribe(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo,
                   uint16_t subValue) override {
    Serial.printf("[BLE] CMD subscribe (unexpected)  handle=%d subValue=%d\n",
                  connInfo.getConnHandle(), subValue);
  }
} settingsCallbacks;

/// @brief Internal struct to be passed in the message queues to wakup the BLE task
struct WakeupMessage {
  enum Reason { PERIODIC, FANET_RX, GPS_GPGGA, GPS_GPRMC } reason;
  using MessageVariant = etl::variant<NMEAString, FanetPacket>;
  MessageVariant message;

  WakeupMessage(Reason reason, MessageVariant message) : reason(reason), message(message) {}
  WakeupMessage(Reason reason) : reason(reason) {}
  WakeupMessage() { reason = Reason::PERIODIC; }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.printf("[BLE] Client connected  handle=%d addr=%s\n",
                  connInfo.getConnHandle(), connInfo.getAddress().toString().c_str());
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("[BLE] Client disconnected  handle=%d reason=0x%03X\n",
                  connInfo.getConnHandle(), reason);
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
    Serial.printf("[BLE] MTU changed  handle=%d mtu=%d\n",
                  connInfo.getConnHandle(), MTU);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    Serial.printf("[BLE] Auth complete  handle=%d authenticated=%d bonded=%d\n",
                  connInfo.getConnHandle(),
                  connInfo.isAuthenticated(),
                  connInfo.isBonded());
  }

  void onConnParamsUpdate(NimBLEConnInfo& connInfo) override {
    Serial.printf("[BLE] Conn params updated  handle=%d interval=%d latency=%d timeout=%d\n",
                  connInfo.getConnHandle(),
                  connInfo.getConnInterval(),
                  connInfo.getConnLatency(),
                  connInfo.getConnTimeout());
  }

} serverCallbacks;

class RspCharCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo,
                   uint16_t subValue) override {
    // subValue: 0=unsubscribe, 1=notify, 2=indicate
    Serial.printf("[BLE] RSP subscribe  handle=%d subValue=%d\n",
                  connInfo.getConnHandle(), subValue);
  }

  void onRead(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    Serial.printf("[BLE] RSP direct read  handle=%d\n", connInfo.getConnHandle());
  }

  void onStatus(NimBLECharacteristic* pChar, int code) override {
    Serial.printf("[BLE] RSP notify status  code=%d\n", code);
  }
} rspCharCallbacks;

BLE& BLE::get() {
  static BLE instance;
  return instance;
}

void BLE::setup() {
  // Initialize the BLE with the unique device name
  etl::string<13> name = "Leaf: ";
  name += FanetRadio::getAddress().c_str();
  NimBLEDevice::init(name.c_str());

  // Create a server using the callback class to re-advertise on a disconnect
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  // Create the characteristic and start advertising climb rate information
  NimBLEService* pService = pServer->createService(LEAF_SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(LEAF_LK8EX1_UUID,
                                                   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pService->start();

  // Settings service — Web Bluetooth client reads/writes all device settings
  NimBLEDevice::setMTU(512);
  pSettingsService = pServer->createService(LEAF_SETTINGS_SVC_UUID);
  pSettingsCmdChar = pSettingsService->createCharacteristic(LEAF_SETTINGS_CMD_UUID,
                                                            NIMBLE_PROPERTY::WRITE);
  pSettingsRspChar = pSettingsService->createCharacteristic(LEAF_SETTINGS_RSP_UUID,
                                                            NIMBLE_PROPERTY::READ |
                                                            NIMBLE_PROPERTY::NOTIFY);
  pSettingsCmdChar->setCallbacks(&settingsCallbacks);
  pSettingsRspChar->setCallbacks(&rspCharCallbacks);
  pSettingsService->start();

  /** Create an advertising instance and add the services to the advertised data */
  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(name.c_str());
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->addServiceUUID(pSettingsService->getUUID());
  /**
   *  If your device is battery powered you may consider setting scan response
   *  to false as it will extend battery life at the expense of less data sent.
   */
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();

  // Setup the FreeRTOS Tasks and Timers associated with this module
  // Create a queue the size of a couple of WakeupMessage length.
  // If say a GPS and periodic send request comes in too close together
  // one of them may be dropped for this cycle.
  xQueue = xQueueCreate(4, sizeof(WakeupMessage));

  // Create the freeRTOS Task for handling Bluetooth low energy IO
  xTaskCreate(BLE::bleTask, "BLE", 10000, this, 9, &xTask);

  // Fire off the timer to periodically request a send
  xTimer = xTimerCreate("BLEPeriodicSend", pdMS_TO_TICKS(100), pdTRUE, NULL, BLE::timerCallback);
  xTimerStart(xTimer, 0);
}

uint8_t checksum(std::string_view string) {
  uint8_t result = 0;
  for (int i = 1; i < string.find('*'); i++) {
    result ^= string[i];
  }
  return result;
}

void BLE::start() {
  pAdvertising->start();
  started = true;
}

void BLE::stop() {
  pAdvertising->stop();
  started = false;
}

void BLE::enableSettingsService(uint32_t pin) {
  settings_service_active = true;
  settings_pin = pin;
}

void BLE::disableSettingsService() {
  settings_service_active = false;
  settings_pin = 0;
}

void BLE::end() {
  // Delete FreeRTOS objects
  xTimerDelete(xTimer, 0);
  vTaskDelete(xTask);
  vQueueDelete(xQueue);

  // Delete any objects created and deinit manually.. Seems to crash setting this to true
  NimBLEDevice::deinit(false);

  // Reset null pointers.
  pServer = nullptr;
  pService = nullptr;
  pCharacteristic = nullptr;
  pSettingsService = nullptr;
  pSettingsCmdChar = nullptr;
  pSettingsRspChar = nullptr;
  pAdvertising = nullptr;
}

void BLE::on_receive(const GpsMessage& msg) {
  // Short circuit if not initialized
  if (pServer == nullptr) return;

  // If the GPS message is a GPGGA or GPRMC, we store it in the buffers
  // for the next periodic send.
  if (msg.nmea.substr(0, 6) == "$GPGGA" || msg.nmea.substr(0, 6) == "$GNGGA") {
    WakeupMessage message(WakeupMessage::Reason::GPS_GPGGA, msg.nmea);
    xQueueSend(BLE::get().xQueue, &message, 0);
  } else if (msg.nmea.substr(0, 6) == "$GPRMC" || msg.nmea.substr(0, 6) == "$GNRMC") {
    WakeupMessage message(WakeupMessage::Reason::GPS_GPRMC, msg.nmea);
    xQueueSend(BLE::get().xQueue, &message, 0);
  }
}

void BLE::on_receive(const FanetPacket& msg) {
  // Short circuit if not initialized
  if (pServer == nullptr) return;

  WakeupMessage message(WakeupMessage::Reason::FANET_RX, msg);
  xQueueSend(BLE::get().xQueue, &message, 0);
}

// FreeRTOS Task
void BLE::bleTask(void* args) {
  BLE* ble = (BLE*)args;  // Bluetooth instance that started this task
  WakeupMessage message;  // Reason for waking up, message to send out
  while (true) {
    // Sleep until there's some message to send out.
    xQueueReceive(ble->xQueue, &message, portMAX_DELAY);
    switch (message.reason) {
      case WakeupMessage::Reason::PERIODIC:
        // Periodic wakeup to send out the last known Vario & Baro data.
        ble->sendVarioUpdate();
        break;
      case WakeupMessage::Reason::FANET_RX:
        ble->sendFanetUpdate(etl::get<FanetPacket>(message.message));
        break;
      case WakeupMessage::Reason::GPS_GPGGA: {
        if (millis() - ble->lastGpsGgaMs < 500) {
          // If we received a GPGGA too soon, skip it
          continue;
        }
        auto& gpsGpggaBuffer = etl::get<NMEAString>(message.message);
        ble->pCharacteristic->setValue((const uint8_t*)gpsGpggaBuffer.c_str(),
                                       gpsGpggaBuffer.size());
        ble->pCharacteristic->notify();
        ble->lastGpsGgaMs = millis();
      } break;
      case WakeupMessage::Reason::GPS_GPRMC: {
        if (millis() - ble->lastGpsGprmcMs < 500) {
          // If we received a GPRMC too soon, skip it
          continue;
        }
        auto& gpsGprmcBuffer = etl::get<NMEAString>(message.message);
        ble->pCharacteristic->setValue((const uint8_t*)gpsGprmcBuffer.c_str(),
                                       gpsGprmcBuffer.size());
        ble->pCharacteristic->notify();

        ble->lastGpsGprmcMs = millis();
        break;
      }
    }
  }
}

void BLE::timerCallback(TimerHandle_t timer) {
  // Send a message on the queue that it's time to do a periodic task send
  // (wake up the BLE task)
  WakeupMessage message(WakeupMessage::Reason::PERIODIC);
  xQueueSend(BLE::get().xQueue, &message, 0);
}

void BLE::sendVarioUpdate() {
  if (baro.state() != Barometer::State::Ready) return;
  NMEAString nmea;
  etl::string_stream stream(nmea);
  int32_t climbRate = baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0;
  stream << "$LK8EX1," << static_cast<int32_t>(baro.pressure()) << ","
         << static_cast<uint>(baro.altF()) << "," << climbRate << ","
         << "99,999,";  // Temperature in C.  If not available, send 99
                        // Battery voltage OR percentage.  If percentage, add 1000 (if 1014 is
                        // 14%). 999

  addChecksumToNMEA(nmea);
  pCharacteristic->setValue((const uint8_t*)nmea.c_str(), nmea.size());
  pCharacteristic->notify();
}

void BLE::sendFanetUpdate(FanetPacket& msg) {
  // Is processed when a Fanet packet is received
  // We only want to send BLE updates if it's a Tracking update

  auto& packet = msg.packet;
  if (packet.header().type() != FANET::Header::MessageType::TRACKING) {
    return;
  }

  auto& payload = etl::get<FANET::TrackingPayload>(packet.payload().value());

  // PFLAA lines to notify where the traffic is
  // PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,
  // <RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,
  // <ClimbRate>,<AcftType>[,<NoTrack>[,<Source>,<RSSI>]]
  // See https://
  // www.flarm.com/wp-content/uploads/2024/04/FTD-012-Data-Port-Interface-Control-Document-ICD-7.19.pdf

  NMEAString stringified;
  etl::string_stream stream(stringified);

  // Aircraft type does not marry up between PFLAA and Fanet types
  char aircraftType;
  switch (payload.aircraftType()) {
    case FANET::TrackingPayload::AircraftType::GLIDER:
      aircraftType = '6';  //  hang glider (hard)
      break;
    case FANET::TrackingPayload::AircraftType::PARAGLIDER:
      aircraftType = '7';  // paraglider (soft)
      break;
    default:
      aircraftType = 'A';
      break;
  }

  // Calculate the difference compared to our position
  double eastOffset = 0;
  double northOffset = 0;
  double gpsAltitude = 0;

  {
    // Create a lock, and work out our offsets
    GpsLockGuard gpsMutex;
    if (!gps.location.isValid()) {
      return;
    }
    constexpr auto EarthRadius = 6378137;

    double dLat = (payload.latitude() - gps.location.lat()) * PI / 180.0;
    double dLon = (payload.longitude() - gps.location.lng()) * PI / 180.0;

    // Convert latitude to radians for scaling factor
    double latAvg = (payload.latitude() + gps.location.lat()) * 0.5 * PI / 180.0;

    northOffset = dLat * EarthRadius;
    eastOffset = dLon * EarthRadius * cos(latAvg);

    gpsAltitude = gps.altitude.meters();
  }

  // Example of one that works: $PFLAA,0,-4,9,-3,2,FB5F20,98,,0,0.0,7,0*0B
  char speedBuf[16];
  char climbBuf[16];

  // Format the floats
  snprintf(speedBuf, sizeof(speedBuf), "%.2f", payload.speed() / 3.6);
  snprintf(climbBuf, sizeof(climbBuf), "%.2f", payload.climbRate());

  // Now use them in the stream
  stream << "$PFLAA,"                             // FLARM/FANET Aircraft Update
         << 0 << ","                              // 0 means no alarm, informational
         << static_cast<int>(northOffset) << ","  // Relative north in meters
         << static_cast<int>(eastOffset) << ","   // Relative east
         << (payload.altitude() - static_cast<int>(gpsAltitude)) << ","  // Relative vertical
         << 2 << ","                                                     // IDType
         << FanetAddressToString(packet.source()).c_str() << ","         // ID of aircraft
         << static_cast<int>(payload.groundTrack()) << ","               // Track heading
         << ","                                                          // Turn rate
         << speedBuf << ","                                              // Ground speed
         << climbBuf << ","                                              // Climb rate
         << etl::string_view(&aircraftType, 1) << ","                    // Aircraft type
         << (payload.tracking() ? 0 : 1) << ","                          // No track
         << 0 << ","                                                     // source is FLARM
         << msg.rssi;                                                    // RSSI

  addChecksumToNMEA(stringified);
  pCharacteristic->setValue((const uint8_t*)stringified.c_str(), stringified.size());
  pCharacteristic->notify();
  Serial.println(stringified.c_str());
}

void BLE::addChecksumToNMEA(etl::istring& nmea) {
  const char hexChars[] = "0123456789ABCDEF";
  uint16_t chk = 0, i = 1;
  while (nmea[i] && nmea[i] != '*') {
    chk ^= nmea[i];
    i++;
  }

  if (i > (nmea.capacity() - 5)) {
    return;
  }
  nmea.resize(i);

  char checksumSuffix[] = {
      '*', hexChars[(chk >> 4) & 0x0F], hexChars[chk & 0x0F], '\r', '\n',
  };

  nmea.append(checksumSuffix, 5);
}
