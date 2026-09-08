#pragma once

#include <array>
#include <atomic>

#include <NimBLEDevice.h>
#include "TinyGPSPlus.h"
#include "etl/variant.h"

#include "dispatch/message_sink.h"
#include "dispatch/message_types.h"

// FreeRTOS Task for handling Bluetooth Operations
class BLE : public MessageSink<BLE, GpsMessage, FanetPacket> {
 public:
  static BLE& get();

  /// @brief Sets up the Bluetooth instance.  Allocates memory, but does not start advertising
  void setup();

  /// @brief Starts advertising
  void start();

  /// @brief Stops advertising, does not free up resources
  void stop();

  /// @brief True when BLE should accept connections, including while already connected
  bool isStarted() const { return started; }

  /// @brief Last runtime state sampled by the BLE task, safe to read from the display task
  const char* statusText() const;

  /// @brief True when BLE resources are currently allocated
  bool isSetup() const { return pServer != nullptr; }

  /// @brief ends the service, tears down bluetooth resources
  void end();

  // MessageSink<BLE, GpsMessage, FanetPacket>
  void on_receive(const GpsMessage& msg);
  void on_receive(const FanetPacket& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

 private:
  enum class State : uint8_t { Off, Waiting, Advertising, Connected };
  enum class WakeupReason : uint8_t { PERIODIC, FANET_RX, GPS_GPGGA, GPS_GPRMC };

  struct WakeupMessage {
    WakeupReason reason = WakeupReason::PERIODIC;
    etl::variant<NMEAString, FanetPacket> message;
  };

  static constexpr size_t QUEUE_CAPACITY = 4;

  BLE()
      : pServer(nullptr),
        pService(nullptr),
        pRxCharacteristic(nullptr),
        pCharacteristic(nullptr),
        pAdvertising(nullptr),
        xQueue(nullptr),
        xFreeQueue(nullptr),
        xTimer(nullptr),
        xTask(nullptr),
        started(false) {}

  std::atomic<bool> started;  // Desired state; advertising pauses while a central is connected.
  std::atomic<State> state_{State::Off};
  // Serialize advertising attempts with start/stop so a retry cannot undo a user disabling BLE.
  SemaphoreHandle_t xAdvertisingMutex = nullptr;

  NimBLEServer* pServer;
  NimBLEService* pService;
  // Nordic UART RX is intentionally receive-only for now: clients may write, but Leaf ignores it.
  NimBLECharacteristic* pRxCharacteristic;
  // Nordic UART TX carries CRLF-terminated LK8EX1, GPS, and PFLAA sentences.
  NimBLECharacteristic* pCharacteristic;
  NimBLEAdvertising* pAdvertising;

  // The work queue carries pointers because WakeupMessage contains self-referential ETL objects
  // that cannot safely be byte-copied by a FreeRTOS queue. The free queue owns the available slots.
  QueueHandle_t xQueue;
  QueueHandle_t xFreeQueue;
  std::array<WakeupMessage, QUEUE_CAPACITY> messagePool_;
  // Timer for periodically requesting an update be sent for our Baro updates
  TimerHandle_t xTimer;
  // Task to handle Bluetooth IO
  TaskHandle_t xTask;

  // FreeRTOS Task handler callbacks
  static void bleTask(void*);
  static void timerCallback(TimerHandle_t timer);

  bool enqueue(WakeupReason reason);
  bool enqueue(WakeupReason reason, const NMEAString& nmea);
  bool enqueue(WakeupReason reason, const FanetPacket& packet);
  void release(WakeupMessage* message);
  static bool ownsInlineBuffer(const NMEAString& nmea);

  void sendVarioUpdate();
  void maintainAdvertising();
  void setState(State state);
  // NimBLE reports whether an update was submitted, not final over-the-air delivery.
  void recordNusNotifyResult(bool success);
  void processDiagnostics();
  void sendGpsUpdate(TinyGPSPlus& gps);
  void sendFanetUpdate(FanetPacket& packetMsg);

  /**
   * Add's the checksum and postfix characters to a NMEA string. It may contain an existing checksum
   * that will be overwritten When the capacity is not enough, the result is undefined Note: Must
   * start with the prefix character $ (for performance reasons)
   * @param nmea example '$PFEC,GPint,RMC05'
   * @return             '$PFEC,GPint,RMC05*2D\r\n'
   */
  void addChecksumToNMEA(etl::istring& nmea);

  unsigned long lastGpsGgaMs = 0;
  unsigned long lastGpsGprmcMs = 0;
  unsigned long lastBleHeapCheckMs = 0;
  unsigned long lastAdvertisingAttemptMs = 0;
};
