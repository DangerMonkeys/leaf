#pragma once

#include <NimBLEDevice.h>

class BLE {
 public:
  static BLE& get();

  void setup();
  void loop();
  void end();

 private:
  BLE() : pServer(nullptr), pService(nullptr), pCharacteristic(nullptr), pAdvertising(nullptr) {}

  NimBLEServer* pServer;
  NimBLEService* pService;
  NimBLECharacteristic* pCharacteristic;
  NimBLEAdvertising* pAdvertising;
};
;
