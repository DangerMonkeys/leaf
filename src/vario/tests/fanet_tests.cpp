#include "tests/fanet_tests.h"

#include <Arduino.h>
#include "comms/fanet_neighbors.h"
#include "dispatch/message_types.h"
#include "etl/message_bus.h"
#include "fanet/address.hpp"
#include "fanet/header.hpp"
#include "fanet/packet.hpp"

/**
 * @brief Test that verifies we don't crash on non-location FANET packets (Issue #306)
 *
 * Previously, receiving ACK/NAME/MESSAGE packets would crash when trying to log
 * with an empty distanceKm optional. This test verifies the fix works.
 */
void test_fanet_neighbors_empty_distance(etl::imessage_bus* bus) {
  Serial.println("\n=== Testing FANET Non-Location Packet Handling (Issue #306) ===");

  FanetNeighbors fanetNeighbors;
  fanetNeighbors.publishTo(bus);

  // Create test neighbor
  FANET::Address testAddress(0x12, 0x3456);
  FanetNeighbors::Neighbor testNeighbor;
  testNeighbor.address = testAddress;
  testNeighbor.lastSeen = millis();
  testNeighbor.rssi = -50.0f;
  testNeighbor.snr = 10.0f;
  testNeighbor.distanceKm = etl::nullopt;  // No distance set
  testNeighbor.groundTrackingMode = etl::nullopt;

#ifdef UNIT_TESTING
  fanetNeighbors.addNeighborForTesting(testNeighbor);

  // Create ACK packet (non-location type that doesn't set distanceKm)
  FANET::Header ackHeader(false, false, FANET::Header::MessageType::ACK);
  FANET::Packet<FANET_MAX_FRAME_SIZE> ackPacket(ackHeader, testAddress, etl::nullopt, etl::nullopt,
                                                etl::nullopt, etl::nullopt);

  FanetPacket msg(ackPacket, -55.0f, 8.5f);

  Serial.println("Sending ACK packet (should not crash)...");
  fanetNeighbors.on_receive(msg);

  Serial.println("[PASS] No crash on non-location packet!");
#else
  Serial.println("[SKIP] UNIT_TESTING not defined");
#endif
}
