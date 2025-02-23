#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include "FreeRTOS.h"
#include "etl/delegate.h"
#include "etl/mutex.h"
#include "etl/optional.h"
#include "fanetManager.h"
#include "fanetNeighbor.h"
#include "fanet_radio_types.h"

// Helper function to convert an Address to a string
String MacToString(Fanet::Mac address);

/// @brief Fanet radio module singleton class.
// This class will handle the radio module, and will be responsible for
// initializing, sending, and receiving messages. It will also handle the
// periodic transmission of the aircraft's position based on how noisy the
// airwaves are and how many neighbors are around.
class FanetRadio {
 public:
  /// @brief Begins the radio module with the given region, sets up a task to handle incoming
  /// messages
  /// @param region
  static void begin(const FanetRadioRegion& region);

  // Request to cleanly stop the radio module
  static void end();

  // Returns the current state of the radio module
  static FanetRadioState getState();

  /// @brief Changes the tracking mode between ground modes (null, flying)
  static void setTrackingMode(const etl::optional<Fanet::GroundTrackingType::enum_type>& mode);

  /// @brief Gets the current location
  static void setCurrentLocation(const float& lat,
                                 const float& lon,
                                 const uint32_t& alt,
                                 const int& heading,
                                 const float& climbRate,
                                 const float& speedKmh);

  /// @brief Sets a callback function for when a packet is received
  /// @param val callback function
  // You should take care to ensure that this is cheap, as it'll get called in the context
  // / stack of the FanetRadio RX task.
  static void setRxCallback(etl::delegate<void(Fanet::Packet&)> val);

  static void clearRxCallback() { rx_callback.reset(); }

  static Fanet::Stats getStats();

  // Gets the current radio ID
  static String getAddress();

  /// @brief Gets the neighbor table
  static etl::unordered_map<uint32_t, Fanet::Neighbor, FANET_MAX_NEIGHBORS> getNeighborTable();

 private:
  static FanetRadioState state;  // The current state of the radio module

  // Fanet manager used to handle packets and radio modules
  // Mutex to protect the manager from being accessed by multiple tasks
  static SemaphoreHandle_t x_fanet_manager_mutex;
  static Fanet::FanetManager* manager;
  static etl::array<uint8_t, 256> buffer;  // Radio module buffer
  static SX1262* radio;

  // RX Task:
  // Used to handle the process of any incoming packets destined for this
  // application payer
  static TaskHandle_t x_fanet_rx_task;  // Task to handle incoming messages
  // A Rx interrupt to flag packets are ready to be read
  static void onRxIsr();
  // The FreeRTOS task that will handle incoming messages
  static void taskRadioRx(void* pvParameters);
  // Processes a rx'd packet for the radio task
  static void processRxPacket();

  // TX Task:
  // Used to handle the process of sending any queued packets
  // from the library to the radio module
  static TaskHandle_t x_fanet_tx_task;
  static void taskRadioTx(void* pvParameters);
  static volatile bool last_was_tx;

  // Callback when a packet is received
  static etl::optional<etl::delegate<void(Fanet::Packet&)>> rx_callback;
};
