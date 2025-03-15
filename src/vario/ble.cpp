#include "ble.h"
#include <Arduino.h>

#include <NimBLEDevice.h>
#include "TinyGPSPlus.h"
#include "baro.h"
#include "etl/string.h"
#include "etl/variant.h"
#include "fanet_radio.h"
#include "gps.h"

// These new unique UUIDs came from
// https://www.uuidgenerator.net/

#define LEAF_SERVICE_UUID "92b38cf3-4bd0-428c-aca4-ae6c3a586835"
// LK8EX1 Telemetry notification
#define LEAF_LK8EX1_UUID "583918da-fb9b-4645-bbaf-2db3b1a9c1b3"

/// @brief Internal struct to be passed in the message queues to wakup the BLE task
struct WakeupMessage {
  enum Reason { PERIODIC, GPS_RX, FANET_RX } reason;
  using MessageVariant = etl::variant<TinyGPSPlus, FanetPacket>;
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
  // Create a queue the size of two WakeupMessage length.
  // If say a GPS and periodic send request comes in too close together
  // one of them may be dropped for this cycle.
  xQueue = xQueueCreate(2, sizeof(WakeupMessage));

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

void BLE::on_receive(const GpsReading& msg) {
  // Only process GPS updates twice a second at most.
  if (millis() - lastGpsMs < 500) {
    return;
  }
  lastGpsMs = millis();

  WakeupMessage message(WakeupMessage::Reason::GPS_RX, msg.gps);
  xQueueSend(BLE::get().xQueue, &message, 0);
}

void BLE::on_receive(const FanetPacket& msg) {
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
      case WakeupMessage::Reason::GPS_RX:
        // A GPS Position update to go out.
        ble->sendGpsUpdate(etl::get<TinyGPSPlus>(message.message));
        break;
      case WakeupMessage::Reason::FANET_RX:
        ble->sendFanetUpdate(etl::get<FanetPacket>(message.message));
        break;
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
  char stringified[100];
  snprintf(stringified, sizeof(stringified),
           // See https://raw.githubusercontent.com/LK8000/LK8000/master/Docs/LK8EX1.txt
           ("$LK8EX1,"  // Type of update
            "%u,"       // raw pressure in hPascal
            "99999,"  // altitude in meters, relative to QNH 1013.25. 99999 if not available/needed
            "%d,"     // Climb rate in cm/s.  Can be 99999 if not available
            "99,"     // Temperature in C.  If not available, send 99
            "999,"  // Battery voltage OR percentage.  If percentage, add 1000 (if 1014 is 14%). 999
                    // if unavailable
            "*"     // Checksum to follow
            ),
           baro.pressureFiltered, baro.climbRateFiltered);
  // Add checksum and delimeter with newline
  snprintf(stringified + strlen(stringified), sizeof(stringified), "%02X\n", checksum(stringified));
  pCharacteristic->setValue((const uint8_t*)stringified, sizeof(stringified));
  pCharacteristic->notify();
}

String formatLatitude(double latitude) {
  char buffer[10];
  char latDir = (latitude >= 0) ? 'N' : 'S';
  latitude = abs(latitude);

  int degrees = (int)latitude;
  double minutes = (latitude - degrees) * 60;

  snprintf(buffer, sizeof(buffer), "%02d%05.2f", degrees, minutes);

  return String(buffer) + "," + latDir;
}

String formatLongitude(double longitude) {
  char buffer[11];
  char lonDir = (longitude >= 0) ? 'E' : 'W';
  longitude = abs(longitude);

  int degrees = (int)longitude;
  double minutes = (longitude - degrees) * 60;

  snprintf(buffer, sizeof(buffer), "%03d%05.2f", degrees, minutes);

  return String(buffer) + "," + lonDir;
}

void BLE::sendGpsUpdate(TinyGPSPlus& gps) {
  // Sends out a GPS update string.  The example is:
  // $GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598, ,*10
  char stringified[100];

  snprintf(stringified, sizeof(stringified),
           "$GPRMC,%02d%02d%02d.%03d,A,%s,%s,%.2f,%.2f,%02d%02d%02d,,*", gps.time.hour(),
           gps.time.minute(), gps.time.second(),
           gps.time.centisecond() / 10,  // Adjust if needed
           formatLatitude(gps.location.lat()), formatLongitude(gps.location.lng()),
           gps.speed.knots(), gps.course.deg(), gps.date.day(), gps.date.month(),
           gps.date.year() % 100);

  snprintf(stringified + strlen(stringified), sizeof(stringified) - strlen(stringified), "%02X\n",
           checksum(stringified));
  Serial.println(stringified);
  pCharacteristic->setValue((const uint8_t*)stringified, strlen(stringified));
  pCharacteristic->notify();
}

void BLE::sendFanetUpdate(FanetPacket& msg) {
  // Is processed when a Fanet packet is received
  // We only want to send BLE updates if it's a Tracking update

  auto& packet = msg.packet;
  if (packet.header.type != Fanet::PacketType::Tracking) {
    return;
  }
  auto& payload = etl::get<Fanet::Tracking>(packet.payload);

  // PFLAA lines to notify where the traffic is
  // PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,
  // <RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,
  // <ClimbRate>,<AcftType>[,<NoTrack>[,<Source>,<RSSI>]]
  // See
  // https://www.flarm.com/wp-content/uploads/2024/04/FTD-012-Data-Port-Interface-Control-Document-ICD-7.19.pdf

  char stringified[100];

  // Aircraft type does not marry up between PFLAA and Fanet types
  char aircraftType;
  switch (payload.aircraftType) {
    case Fanet::AircraftType::Glider:
      aircraftType = '6';  //  hang glider (hard)
      break;
    case Fanet::AircraftType::Paraglider:
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

    double dLat = (payload.location.latitude - gps.location.lat()) * PI / 180.0;
    double dLon = (payload.location.longitude - gps.location.lng()) * PI / 180.0;

    // Convert latitude to radians for scaling factor
    double latAvg = (payload.location.latitude + gps.location.lat()) * 0.5 * PI / 180.0;

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
           (int)northOffset, (int)eastOffset, payload.altitude - (int)gpsAltitude,
           MacToString(packet.header.srcMac), payload.heading, payload.speed / 3.6,
           payload.climbRate, aircraftType, payload.onlineTracking ? 0 : 1, msg.rssi);

  Serial.println(stringified);
  pCharacteristic->setValue((const uint8_t*)stringified, strlen(stringified));
  pCharacteristic->notify();
}
