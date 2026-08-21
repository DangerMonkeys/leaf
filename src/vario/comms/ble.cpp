#include "comms/ble.h"

#include <Arduino.h>
#include <algorithm>
#include <atomic>

#include <NimBLEDevice.h>
#include "TinyGPSPlus.h"
#include "comms/fanet_radio.h"
#include "comms/webserver.h"
#include "diagnostics/diagnostic_logs.h"
#include "diagnostics/heap_monitor.h"
#include "esp_heap_caps.h"
#include "etl/string.h"
#include "etl/string_stream.h"
#include "etl/variant.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "power.h"
#include "utils/lock_guard.h"

// These UUIDs are for BLE UART services and characteristics.
// This is required to be UART due to a requirement for
// compatibility with SeeYou Navigator.
#define LEAF_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // Nordic UART service
#define LEAF_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"       // Central-to-Leaf writes
#define LEAF_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"       // Leaf-to-central notifications

namespace {
  constexpr unsigned long BLE_HEAP_CHECK_INTERVAL_MS = 5000;

  enum BleDiagnosticEvent : uint32_t {
    BLE_DIAG_CONNECTED = 1 << 0,
    BLE_DIAG_DISCONNECTED = 1 << 1,
    BLE_DIAG_ADV_RESTARTED = 1 << 2,
    BLE_DIAG_ADV_RESTART_FAILED = 1 << 3,
    BLE_DIAG_NUS_NOTIFY_FAILED = 1 << 4,
    BLE_DIAG_PERIODIC_QUEUE_FULL = 1 << 5,
    BLE_DIAG_GPS_QUEUE_FULL = 1 << 6,
    BLE_DIAG_FANET_QUEUE_FULL = 1 << 7,
  };

  std::atomic<uint32_t> pendingBleDiagnosticEvents{0};
  std::atomic<int> lastBleDisconnectReason{0};
  std::atomic<uint32_t> nusNotifySuccessCount{0};
  std::atomic<uint32_t> nusNotifyFailureCount{0};

  void markBleDiagnosticEvent(BleDiagnosticEvent event) {
    pendingBleDiagnosticEvents.fetch_or(static_cast<uint32_t>(event), std::memory_order_relaxed);
  }
}  // namespace

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
  // Not sure we need this.  Taken from the demo
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    /**
     *  We can use the connection handle here to ask for different connection parameters.
     *  Args: connection handle, min connection interval, max connection interval
     *  latency, supervision timeout.
     *  Units; Min/Max Intervals: 1.25 millisecond increments.
     *  Latency: number of intervals allowed to skip.
     *  Timeout: 10 millisecond increments.
     */
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
    markBleDiagnosticEvent(BLE_DIAG_CONNECTED);
  }

  // This one seems import to re-advertise
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    lastBleDisconnectReason.store(reason, std::memory_order_relaxed);
    markBleDiagnosticEvent(BLE_DIAG_DISCONNECTED);
    if (BLE::get().isStarted()) {
      // Re-advertise after a disconnect when BLE is enabled.
      markBleDiagnosticEvent(NimBLEDevice::startAdvertising() ? BLE_DIAG_ADV_RESTARTED
                                                              : BLE_DIAG_ADV_RESTART_FAILED);
    }
  }

} serverCallbacks;

BLE& BLE::get() {
  static BLE instance;
  return instance;
}

void BLE::setup() {
  if (pServer != nullptr) return;

  heap_monitor::checkpoint("ble-setup-before");

  // Initialize BLE with the same unique, user-visible name as the Leaf AP.
  const String name = webserver_leaf_ap_ssid();
  NimBLEDevice::init(name.c_str());

  // Create a server using the callback class to re-advertise on a disconnect
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  // Expose the standard Nordic UART RX characteristic for BLE serial clients such as XCSoar.
  // Leaf does not currently process incoming data, so the default callback is intentionally used.
  pService = pServer->createService(LEAF_SERVICE_UUID);
  pRxCharacteristic = pService->createCharacteristic(LEAF_RX_UUID, NIMBLE_PROPERTY::WRITE_NR, 64);
  pCharacteristic =
      pService->createCharacteristic(LEAF_TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pService->start();

  /** Create an advertising instance and add the services to the advertised data */
  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(name.c_str());
  pAdvertising->addServiceUUID(pService->getUUID());
  /**
   *  If your device is battery powered you may consider setting scan response
   *  to false as it will extend battery life at the expense of less data sent.
   */
  pAdvertising->enableScanResponse(true);

  // Setup the FreeRTOS Tasks and Timers associated with this module
  // Create a queue the size of a couple of WakeupMessage length.
  // If say a GPS and periodic send request comes in too close together
  // one of them may be dropped for this cycle.
  xQueue = xQueueCreate(4, sizeof(WakeupMessage));
  heap_monitor::checkpoint("ble-queue");

  // Create the freeRTOS Task for handling Bluetooth low energy IO
  xTaskCreate(BLE::bleTask, "BLE", 5120, this, 9, &xTask);
  heap_monitor::registerTask("ble", xTask);
  heap_monitor::checkpoint("ble-task");

  // Fire off the timer to periodically request a send
  xTimer = xTimerCreate("BLEPeriodicSend", pdMS_TO_TICKS(100), pdTRUE, NULL, BLE::timerCallback);
  xTimerStart(xTimer, 0);
  heap_monitor::checkpoint("ble-setup-after");
}

uint8_t checksum(std::string_view string) {
  uint8_t result = 0;
  for (int i = 1; i < string.find('*'); i++) {
    result ^= string[i];
  }
  return result;
}

void BLE::start() {
  if (pAdvertising == nullptr) setup();
  if (pAdvertising == nullptr || started) return;
  heap_monitor::checkpoint("ble-start-before");
  pAdvertising->start();
  started = true;
  heap_monitor::checkpoint("ble-start-after");
}

void BLE::stop() {
  if (pAdvertising == nullptr || !started) return;
  heap_monitor::checkpoint("ble-stop-before");
  pAdvertising->stop();
  started = false;
  heap_monitor::checkpoint("ble-stop-after");
}

void BLE::end() {
  if (pServer == nullptr && xTimer == nullptr && xTask == nullptr && xQueue == nullptr) return;

  heap_monitor::checkpoint("ble-end-before");
  if (pAdvertising != nullptr && started) {
    pAdvertising->stop();
  }
  started = false;

  // Delete FreeRTOS objects
  if (xTimer != nullptr) {
    xTimerStop(xTimer, 0);
    xTimerDelete(xTimer, 0);
    xTimer = nullptr;
  }
  if (xTask != nullptr) {
    vTaskDelete(xTask);
    xTask = nullptr;
  }
  if (xQueue != nullptr) {
    vQueueDelete(xQueue);
    xQueue = nullptr;
  }
  heap_monitor::registerTask("ble", nullptr);

  // Delete any objects created and deinit manually.. Seems to crash setting this to true
  if (pServer != nullptr) {
    NimBLEDevice::deinit(false);
  }

  // Reset null pointers.
  pServer = nullptr;
  pService = nullptr;
  pRxCharacteristic = nullptr;
  pCharacteristic = nullptr;
  pAdvertising = nullptr;
  heap_monitor::checkpoint("ble-end-after");
}

void BLE::on_receive(const GpsMessage& msg) {
  // Short circuit if not initialized
  if (pServer == nullptr) return;

  // If the GPS message is a GPGGA or GPRMC, we store it in the buffers
  // for the next periodic send.
  if (msg.nmea.substr(0, 6) == "$GPGGA" || msg.nmea.substr(0, 6) == "$GNGGA") {
    WakeupMessage message(WakeupMessage::Reason::GPS_GPGGA, msg.nmea);
    if (xQueueSend(BLE::get().xQueue, &message, 0) != pdTRUE) {
      markBleDiagnosticEvent(BLE_DIAG_GPS_QUEUE_FULL);
    }
  } else if (msg.nmea.substr(0, 6) == "$GPRMC" || msg.nmea.substr(0, 6) == "$GNRMC") {
    WakeupMessage message(WakeupMessage::Reason::GPS_GPRMC, msg.nmea);
    if (xQueueSend(BLE::get().xQueue, &message, 0) != pdTRUE) {
      markBleDiagnosticEvent(BLE_DIAG_GPS_QUEUE_FULL);
    }
  }
}

void BLE::on_receive(const FanetPacket& msg) {
  // Short circuit if not initialized
  if (pServer == nullptr) return;

  WakeupMessage message(WakeupMessage::Reason::FANET_RX, msg);
  if (xQueueSend(BLE::get().xQueue, &message, 0) != pdTRUE) {
    markBleDiagnosticEvent(BLE_DIAG_FANET_QUEUE_FULL);
  }
}

// FreeRTOS Task
void BLE::bleTask(void* args) {
  BLE* ble = (BLE*)args;  // Bluetooth instance that started this task
  WakeupMessage message;  // Reason for waking up, message to send out
  while (true) {
    // Sleep until there's some message to send out.
    xQueueReceive(ble->xQueue, &message, portMAX_DELAY);
    ble->processDiagnostics();
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
        ble->addChecksumToNMEA(gpsGpggaBuffer);
        ble->pCharacteristic->setValue((const uint8_t*)gpsGpggaBuffer.c_str(),
                                       gpsGpggaBuffer.size());
        ble->recordNusNotifyResult(ble->pCharacteristic->notify());
        ble->lastGpsGgaMs = millis();
      } break;
      case WakeupMessage::Reason::GPS_GPRMC: {
        if (millis() - ble->lastGpsGprmcMs < 500) {
          // If we received a GPRMC too soon, skip it
          continue;
        }
        auto& gpsGprmcBuffer = etl::get<NMEAString>(message.message);
        ble->addChecksumToNMEA(gpsGprmcBuffer);
        ble->pCharacteristic->setValue((const uint8_t*)gpsGprmcBuffer.c_str(),
                                       gpsGprmcBuffer.size());
        ble->recordNusNotifyResult(ble->pCharacteristic->notify());

        ble->lastGpsGprmcMs = millis();
        break;
      }
    }
  }
}

void BLE::timerCallback(TimerHandle_t timer) {
  // Send a message on the queue that it's time to do a periodic task send
  // (wake up the BLE task)
  if (BLE::get().xQueue == nullptr) return;
  WakeupMessage message(WakeupMessage::Reason::PERIODIC);
  if (xQueueSend(BLE::get().xQueue, &message, 0) != pdTRUE) {
    markBleDiagnosticEvent(BLE_DIAG_PERIODIC_QUEUE_FULL);
  }
}

void BLE::sendVarioUpdate() {
  if (baro.state() != Barometer::State::Ready) return;
  NMEAString nmea;
  etl::string_stream stream(nmea);
  int32_t climbRate = baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0;
  char temperature[16] = "99";
  if (ambient.state() == Ambient::State::Ready) {
    snprintf(temperature, sizeof(temperature), "%.1f", ambient.temp());
  }
  const int battery = std::clamp<int>(power.info().batteryPercent, 0, 100);

  stream << "$LK8EX1," << static_cast<int32_t>(baro.pressure()) << ","
         << static_cast<uint>(baro.altF()) << "," << climbRate << "," << temperature << ","
         << (1000 + battery) << ",";

  addChecksumToNMEA(nmea);
  pCharacteristic->setValue((const uint8_t*)nmea.c_str(), nmea.size());
  recordNusNotifyResult(pCharacteristic->notify());
}

void BLE::recordNusNotifyResult(bool success) {
  if (success) {
    nusNotifySuccessCount.fetch_add(1, std::memory_order_relaxed);
  } else {
    nusNotifyFailureCount.fetch_add(1, std::memory_order_relaxed);
    markBleDiagnosticEvent(BLE_DIAG_NUS_NOTIFY_FAILED);
  }
}

void BLE::processDiagnostics() {
  const uint32_t events = pendingBleDiagnosticEvents.exchange(0, std::memory_order_relaxed);
  if (events & BLE_DIAG_CONNECTED) heap_monitor::checkpoint("ble-connected");
  if (events & BLE_DIAG_DISCONNECTED) {
    const int reason = lastBleDisconnectReason.load(std::memory_order_relaxed);
    diagnostic_logs::appendSystemEvent("ble", "disconnected", String(reason), "reason", reason,
                                       true);
    diagnostic_logs::appendSystemEvent(
        "ble", "notify_success", String(), "count",
        static_cast<int32_t>(nusNotifySuccessCount.load(std::memory_order_relaxed)), true);
    diagnostic_logs::appendSystemEvent(
        "ble", "notify_failure", String(), "count",
        static_cast<int32_t>(nusNotifyFailureCount.load(std::memory_order_relaxed)), true);
    heap_monitor::checkpoint("ble-disconnected");
  }
  if (events & BLE_DIAG_ADV_RESTARTED) heap_monitor::checkpoint("ble-adv-restart-ok");
  if (events & BLE_DIAG_ADV_RESTART_FAILED) heap_monitor::checkpoint("ble-adv-restart-fail");
  if (events & BLE_DIAG_NUS_NOTIFY_FAILED) heap_monitor::checkpoint("ble-notify-fail");
  if (events & BLE_DIAG_PERIODIC_QUEUE_FULL) heap_monitor::checkpoint("ble-periodic-q-full");
  if (events & BLE_DIAG_GPS_QUEUE_FULL) heap_monitor::checkpoint("ble-gps-q-full");
  if (events & BLE_DIAG_FANET_QUEUE_FULL) heap_monitor::checkpoint("ble-fanet-q-full");

  const unsigned long now = millis();
  if (now - lastBleHeapCheckMs >= BLE_HEAP_CHECK_INTERVAL_MS) {
    if (!heap_caps_check_integrity_all(false)) {
      heap_monitor::checkpoint("ble-heap-invalid");
    }
    lastBleHeapCheckMs = now;
  }
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
    if (!gps.hasUsableFix()) {
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
  recordNusNotifyResult(pCharacteristic->notify());
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
