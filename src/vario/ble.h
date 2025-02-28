#pragma once

#include <BLEDevice.h>
#include <BLEServer.h>
#include <memory>

class BLE {
  public:
    static BLE& get();

    void setup();
    void loop();

  private:
    BLE() = default;

    std::unique_ptr<BLEAdvertising> advertising = nullptr;

    std::unique_ptr<BLEServer> server = nullptr;
    std::unique_ptr<BLEService> vario_service = nullptr;
    std::unique_ptr<BLECharacteristic> vario_characteristic = nullptr;
    std::unique_ptr<BLECharacteristic> vario_characteristic_rx = nullptr;
};
