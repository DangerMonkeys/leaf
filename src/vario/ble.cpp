#include "ble.h"
#include <Arduino.h>

#include <NimBLEDevice.h>
#include "baro.h"
#include "etl/string.h"
#include "fanet_radio.h"

// These new unique UUIDs came from
// https://www.uuidgenerator.net/

#define LEAF_SERVICE_UUID "92b38cf3-4bd0-428c-aca4-ae6c3a586835"
// LK8EX1 Telemetry notification
#define LEAF_LK8EX1_UUID "583918da-fb9b-4645-bbaf-2db3b1a9c1b3"

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
}

uint8_t checksum(std::string_view string) {
  uint8_t result = 0;
  for (int i = 1; i < string.find('*'); i++) {
    result ^= string[i];
  }
  return result;
}

void BLE::loop() {
  // Short circuit if not running or no connected clients
  if (!pServer || !pServer->getConnectedCount()) return;

  char stringified[50];
  snprintf(stringified,
           sizeof(stringified),
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
           baro.pressureFiltered,
           baro.climbRateFiltered);
  // Add checksum and delimeter with newline
  snprintf(stringified + strlen(stringified), sizeof(stringified), "%02X\n", checksum(stringified));
  pCharacteristic->setValue((const uint8_t*)stringified, sizeof(stringified));
  pCharacteristic->notify();

  // TODO:  Need lines for GPS and Fanet updates
}

void BLE::end() {
  // Delete any objects created and deinit manually.. Seems to crash setting this to true
  NimBLEDevice::deinit(false);

  // Reset null pointers.
  pServer = nullptr;
  pService = nullptr;
  pCharacteristic = nullptr;
  pAdvertising = nullptr;
}
