#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include "FreeRTOS.h"
#include "comms/debug_webserver.h"
#include "comms/fanet_neighbors.h"
#include "comms/fanet_radio_types.h"
#include "comms/message_types.h"
#include "etl/delegate.h"
#include "etl/message_bus.h"
#include "etl/message_router.h"
#include "etl/mutex.h"
#include "etl/optional.h"
#include "fanet/connector.hpp"
#include "fanet/groundTracking.hpp"
#include "fanet/protocol.hpp"
#include "ui/settings/settings.h"

// Helper function to convert an Address to a string
String FanetAddressToString(FANET::Address address);

/// @brief Fanet radio module singleton class.
// This class will handle the radio module, and will be responsible for
// initializing, sending, and receiving messages. It will also handle the
// periodic transmission of the aircraft's position based on how noisy the
// airwaves are and how many neighbors are around.
class FanetRadio : public etl::message_router<FanetRadio, GpsReading>, public FANET::Connector {
  // Allow the debug webserver to access all of our private parts
  friend void webserver_setup();

 public:
  // Sets up the FANET Radio connector
  uint32_t fanet_getTick() const override { return millis(); }
  bool fanet_sendFrame(uint8_t codingRate, etl::span<const uint8_t> data);
  void fanet_ackReceived(uint16_t id) override {}

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
  void setGroundTrackingMode(const FANET::GroundTrackingPayload::TrackingType& mode);

  /// @brief Gets the current location
  void setCurrentLocation(const float& lat, const float& lon, const uint32_t& alt,
                          const int& heading, const float& climbRate, const float& speedKmh);

  const FANET::Protocol::Stats getStats() const;

  // Gets the current radio ID
  static String getAddress();

  /// @brief Gets a copy of the neighbor table
  const FanetNeighbors::NeighborMap& getNeighborTable() const;

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

  FANET::Protocol* protocol = nullptr;  // Pointer to the Fanet manager

  // detect phyiscal presence of the FANET (LoRa SX1262) module
  bool detectFanet(void);

  FanetRadioState state = FanetRadioState::UNINITIALIZED;  // The current state of the radio module

  // Delete copy constructor and copy assignment operator
  FanetRadio(const FanetRadio&) = delete;
  FanetRadio& operator=(const FanetRadio&) = delete;

  // Fanet manager used to handle packets and radio modules
  // Mutex to protect the manager from being accessed by multiple tasks
  SemaphoreHandle_t x_fanet_manager_mutex = nullptr;
  // Fanet::FanetManager* manager = nullptr;
  etl::array<uint8_t, FANET_MAX_FRAME_SIZE> buffer = {0};  // Radio module buffer
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

  // Periodic name sending
  TaskHandle_t x_fanet_tx_name_task = nullptr;
  static void taskRadioNameTx(void* pvParameters);

  /// @brief Setup of the Fanet Packet Handler
  void setupFanetHandler();

  /// @brief Message bus to write events onto
  etl::imessage_bus* bus = nullptr;

  /// @brief Time we last flushed out old expired neighbors
  unsigned long neighbor_table_flushed = 0;

  /// @brief Time we're allowed to send out a tracking update
  unsigned long m_nextAllowedTrackingTimeMs = 0;  // Time we're allowed to send a tracking message

  /// @brief Random number generator
  etl::random_xorshift random;

  /// @brief if a frame is currently being sent on the wire.
  static volatile bool frameSending;

  /// @brief The current tracking mode, if any
  etl::optional<FANET::GroundTrackingPayload::TrackingType::enum_type> trackingMode = etl::nullopt;

  FanetNeighbors neighbors;  // The neighbor table with more info than the protocol class

#ifdef LORA_SX1262
  Module radioModule = Module((uint32_t)SX1262_NSS, SX1262_DIO1, SX1262_RESET, SX1262_BUSY);
#endif
};
