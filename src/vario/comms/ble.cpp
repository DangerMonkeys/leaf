#include "comms/ble.h"

#include <Arduino.h>

#include <NimBLEDevice.h>
#include "TinyGPSPlus.h"
#include "comms/fanet_radio.h"
#include "etl/string.h"
#include "etl/string_stream.h"
#include "etl/variant.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "utils/lock_guard.h"

// These UUIDs are for BLE UART services and characteristics.
// This is required to be UART due to a requirement for
// compatibility with SeeYou Navigator.
#define LEAF_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // SPP service
#define LEAF_LK8EX1_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // SPP characteristic (TX/RX)

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
  }

  // This one seems import to re-advertise
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    // Re-advertise after a disconnect
    NimBLEDevice::startAdvertising();
  }

} serverCallbacks;

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

  /** Create an advertising instance and add the services to the advertised data */
  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(name.c_str());
  pAdvertising->addServiceUUID(pService->getUUID());
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
          return;
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
          return;
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
  NMEAString nmea;
  etl::string_stream stream(nmea);
  stream << "$LK8EX1," << static_cast<int32_t>(baro.pressure) << "," << static_cast<uint>(baro.altF)
         << "," << baro.climbRateFiltered << ","
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

  char stringified[100];

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

  snprintf(stringified, sizeof(stringified),
           ("$PFLAA,"
            "0,"       // 0 means no alarm, informational
            "%d,"      // TODO:  Relative north in meters from current location
            "%d,"      // Relative east from current location
            "%d,"      // Relative verticle from current location
            "2,"       // IDType:  2 for Fixed FLARM Id (we'll re-use for Fanet-Flarm)
            "%s,"      // ID of aircraft
            "%d,"      // Track heading?  In degrees
            ","        // Turn rate (currently blank)
            "%.2f,"    // Ground speed in meters per second
            "%.2f,"    // Climb rate (in m/s)
            "%c,"      // Aircraft type
            "%d,"      // No track
            "0,"       // source is FLARM
            "%.2f*\n"  // RSSI
            ),
           (int)northOffset, (int)eastOffset, payload.altitude() - (int)gpsAltitude,
           FanetAddressToString(packet.source()), static_cast<int>(payload.groundTrack()),
           payload.speed() / 3.6, payload.climbRate(), aircraftType, payload.tracking() ? 0 : 1,
           msg.rssi);

  Serial.println(stringified);
  pCharacteristic->setValue((const uint8_t*)stringified, strlen(stringified));
  pCharacteristic->notify();
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