#include <BLEDescriptor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include <string_view>

#include "baro.h"
#include "ble.h"

// It is mandatory to use these Nordic UART Service UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_VARIO_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_VARIO_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLE &BLE::get() {
  static BLE instance;
  return instance;
}

void BLE::setup() {
  BLEDevice::init("MyVario");

  uint8_t temp_val = 0;

  advertising.reset(BLEDevice::getAdvertising());
  server.reset(BLEDevice::createServer());
  vario_service.reset(server->createService(SERVICE_UUID));

  vario_characteristic.reset(vario_service->createCharacteristic(
      CHARACTERISTIC_VARIO_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY));
  vario_characteristic->addDescriptor(
      new BLEDescriptor(BLEUUID((uint16_t)0x2902)));
  vario_characteristic->setValue(&temp_val, sizeof(temp_val));

  vario_characteristic_rx.reset(vario_service->createCharacteristic(
      CHARACTERISTIC_VARIO_UUID_RX,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE));
  vario_characteristic_rx->addDescriptor(
      new BLEDescriptor(BLEUUID((uint16_t)0x2902)));
  vario_characteristic_rx->setValue(&temp_val, sizeof(temp_val));

  vario_service->start();

  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();

  Serial.println("BLE Vario Initialized");
}

uint8_t checksum(std::string_view string) {
  uint8_t result = 0;
  for (int i = 1; i < string.find('*'); i++) {
    result ^= string[i];
  }
  return result;
}

void BLE::loop() {
  char stringified[50];
  snprintf(stringified, sizeof(stringified), "$LK8EX1,%u,99999,%d,99,999,*",
           baro.pressureFiltered, baro.climbRateFiltered);
  snprintf(stringified + strlen(stringified), sizeof(stringified), "%02X",
           checksum(stringified));

  vario_characteristic->setValue((uint8_t *)stringified, strlen(stringified));
  vario_characteristic->notify();
}
