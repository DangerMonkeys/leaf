#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include "DebugWebserver.h"
#include "FreeRTOS.h"
#include "etl/delegate.h"
#include "etl/message_bus.h"
#include "etl/message_router.h"
#include "etl/mutex.h"
#include "etl/optional.h"
#include "fanetManager.h"
#include "fanetNeighbor.h"
#include "fanet_radio_types.h"
#include "message_types.h"
#include "settings.h"

// Helper function to convert an Address to a string
String MacToString(Fanet::Mac address);

/// @brief Fanet radio module singleton class.
// This class will handle the radio module, and will be responsible for
// initializing, sending, and receiving messages. It will also handle the
// periodic transmission of the aircraft's position based on how noisy the
// airwaves are and how many neighbors are around.
class FanetRadio : public etl::message_router<FanetRadio, GpsReading> {
  // Allow the debug webserver to access all of our private parts
  friend void webserver_setup();

 public:
  /// @brief Allocates any dynamic memory required for module.
  void setup(etl::imessage_bus* bus);

  /// @brief Begins the radio module with the given region, sets up a task to handle incoming
  /// messages
  /// @param region
  void begin(const FanetRadioRegion& region);

  // Request to cleanly stop the radio module
  void end();

  // Returns the current state of the radio module
  FanetRadioState getState();

  /// @brief Changes the tracking mode between ground modes (null, flying)
  void setTrackingMode(const etl::optional<Fanet::GroundTrackingType::enum_type>& mode);

  /// @brief Gets the current location
  void setCurrentLocation(const float& lat,
                          const float& lon,
                          const uint32_t& alt,
                          const int& heading,
                          const float& climbRate,
                          const float& speedKmh);

  Fanet::Stats getStats();

  // Gets the current radio ID
  static String getAddress();

  /// @brief Gets the neighbor table
  etl::unordered_map<uint32_t, Fanet::Neighbor, FANET_MAX_NEIGHBORS> getNeighborTable();

  /// @brief Gets the instance of the Fanet Radio handler
  static FanetRadio& getInstance() {
    static FanetRadio instance;
    return instance;
  }

  // Handle GPS Packet updates
  void on_receive(const GpsReading& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

 private:
  // Singleton class
  FanetRadio() : message_router(0) {}

  FanetRadioState state = FanetRadioState::UNINITIALIZED;  // The current state of the radio module

  // Delete copy constructor and copy assignment operator
  FanetRadio(const FanetRadio&) = delete;
  FanetRadio& operator=(const FanetRadio&) = delete;

  // Fanet manager used to handle packets and radio modules
  // Mutex to protect the manager from being accessed by multiple tasks
  SemaphoreHandle_t x_fanet_manager_mutex = nullptr;
  Fanet::FanetManager* manager = nullptr;
  etl::array<uint8_t, 256> buffer = {0};  // Radio module buffer
  SX1262* radio = nullptr;

  // RX Task:
  // Used to handle the process of any incoming packets destined for this
  // application payer
  TaskHandle_t x_fanet_rx_task = nullptr;  // Task to handle incoming messages
  // A Rx interrupt to flag packets are ready to be read
  static void onRxIsr();
  // The FreeRTOS task that will handle incoming messages
  static void taskRadioRx(void* pvParameters);
  // Processes a rx'd packet for the radio task
  void processRxPacket();

  // TX Task:
  // Used to handle the process of sending any queued packets
  // from the library to the radio module
  TaskHandle_t x_fanet_tx_task = nullptr;
  static void taskRadioTx(void* pvParameters);
  static volatile bool last_was_tx;

  // Periodic name sending
  TaskHandle_t x_fanet_tx_name_task = nullptr;
  static void taskRadioNameTx(void* pvParameters);

  /// @brief Setup of the Fanet Packet Handler
  void setupFanetHandler();

  /// @brief Message bus to write events onto
  etl::imessage_bus* bus = nullptr;

  /// @brief Time we last flushed out old expired neighbors
  unsigned long neighbor_table_flushed = 0;

#ifdef LORA_SX1262
  Module radioModule = Module((uint32_t)SX1262_NSS, SX1262_DIO1, SX1262_RESET, SX1262_BUSY);
#endif
};
