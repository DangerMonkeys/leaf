#pragma once

#include <NimBLEDevice.h>
#include "TinyGPSPlus.h"
#include "comms/message_types.h"
#include "etl/message_router.h"

// FreeRTOS Task for handling Bluetooth Operations
class BLE : public etl::message_router<BLE, GpsReading, FanetPacket> {
 public:
  static BLE& get();

  /// @brief Sets up the Bluetooth instance.  Allocates memory, but does not start advertising
  void setup();

  /// @brief Starts advertising
  void start();

  /// @brief Stops advertising, does not free up resources
  void stop();

  /// @brief ends the service, tears down bluetooth resources
  void end();

  // On Receive handlers for the message router.
  void on_receive(const GpsReading& msg);
  void on_receive(const FanetPacket& msg);

  void on_receive_unknown(const etl::imessage& msg) {}

 private:
  BLE()
      : pServer(nullptr),
        pService(nullptr),
        pCharacteristic(nullptr),
        pAdvertising(nullptr),
        started(false),
        message_router(0) {}

  bool started;  // Bluetooth advertising started

  NimBLEServer* pServer;
  NimBLEService* pService;
  NimBLECharacteristic* pCharacteristic;
  NimBLEAdvertising* pAdvertising;

  // Queue for handling messages into this task from either buffer or periodic wakeup events.
  QueueHandle_t xQueue;
  // Timer for periodically requesting an update be sent for our Baro updates
  TimerHandle_t xTimer;
  // Task to handle Bluetooth IO
  TaskHandle_t xTask;

  // FreeRTOS Task handler callbacks
  static void bleTask(void*);
  static void timerCallback(TimerHandle_t timer);

  void sendVarioUpdate();
  void sendGpsUpdate(TinyGPSPlus& gps);
  void sendFanetUpdate(FanetPacket& packetMsg);

  long lastGpsMs = 0;
};
;
