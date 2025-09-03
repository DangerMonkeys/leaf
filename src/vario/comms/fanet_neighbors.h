#pragma once

#include <etl/map.h>
#include <etl/message_router.h>
#include <etl/optional.h>
#include <etl/set.h>
#include "dispatch/message_types.h"
#include "fanet/groundTracking.hpp"
#include "fanet/neighbourTable.hpp"
#include "fanet/protocol.hpp"
#include "instruments/gps.h"

#include "logging/telemetry.h"

/// @brief A class to handle FANET neighbor statistics in addition to base Fanet neighbor table.
struct FanetNeighbors : public etl::message_router<FanetNeighbors, FanetPacket>,
                        public IMessageSource {
 public:
  struct Neighbor {
    FANET::Address address;  // The address of the neighbor
    uint32_t lastSeen;       // The last time this neighbor was seen
    float rssi = 0;          // The RSSI of the last packet received from this neighbor
    float snr = 0;           // The SNR of the last packet received from this neighbor
    etl::optional<float> distanceKm = etl::nullopt;  // The distance to this neighbor in kilometers
    etl::optional<FANET::GroundTrackingPayload::TrackingType> groundTrackingMode =
        etl::nullopt;  // Tracking mode of this neighbor
  };

  using NeighborMap = etl::map<uint32_t, Neighbor, FANET::Protocol::FANET_MAX_NEIGHBORS>;

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

 private:
  NeighborMap neighbors_;
  etl::imessage_bus* bus_ = nullptr;

 public:
  const NeighborMap& get() const { return neighbors_; }

  void updateFromTable(const FANET::NeighbourTable<FANET::Protocol::FANET_MAX_NEIGHBORS>& table) {
    etl::set<uint32_t, FANET::Protocol::FANET_MAX_NEIGHBORS> seen;

    auto libNeighbors = table.neighborTable();

    for (auto& neighbor : libNeighbors) {
      seen.insert(neighbor.address.asUint());
    }

    // Remove neighbors that are not in the given table
    for (auto it = neighbors_.begin(); it != neighbors_.end();) {
      if (seen.find(it->first) == seen.end()) {
        it = neighbors_.erase(it);  // Remove neighbor if not found in the table
      } else {
        // Update lastSeen time for neighbors that are still in the table
        it->second.lastSeen = table.lastSeen(FANET::Address(it->first));
        ++it;  // Move to the next neighbor
      }
    }

    // Insert neighbors in the given table not in the local table
    for (const auto& neighbor : libNeighbors) {
      if (neighbors_.find(neighbor.address.asUint()) == neighbors_.end()) {
        neighbors_[neighbor.address.asUint()] = {neighbor.address, neighbor.lastSeen, 0, 0};
      }
    }
  }

  // Message bus operations.  Receives FanetPacket messages to update location
  void on_receive(const FanetPacket& msg) {
    // Only process FANET packets that are in our local neighbor table
    if (!neighbors_.contains(msg.packet.source().asUint())) {
      return;
    }

    auto& neighbor = neighbors_[msg.packet.source().asUint()];

    // Update the RSSI and SNR values for this packet
    neighbor.rssi = msg.rssi;
    neighbor.snr = msg.snr;

    float lat;
    float lon;

    // Update location for Tracking and GroundTracking modes
    if (msg.packet.header().type() == FANET::Header::MessageType::TRACKING) {
      const auto& trackingPayload = etl::get<FANET::TrackingPayload>(msg.packet.payload().value());

      const auto& longitude = trackingPayload.longitude();
      const auto& latitude = trackingPayload.latitude();

      lat = latitude;
      lon = longitude;

      neighbor.distanceKm =
          gps.distanceBetween(gps.location.lat(), gps.location.lng(), latitude, longitude) / 1000;
      neighbor.groundTrackingMode = etl::nullopt;

    } else if (msg.packet.header().type() == FANET::Header::MessageType::GROUND_TRACKING) {
      const auto& trackingPayload =
          etl::get<FANET::GroundTrackingPayload>(msg.packet.payload().value());

      const auto& longitude = trackingPayload.longitude();
      const auto& latitude = trackingPayload.latitude();

      lat = latitude;
      lon = longitude;

      neighbor.distanceKm =
          gps.distanceBetween(gps.location.lat(), gps.location.lng(), latitude, longitude) / 1000;
      neighbor.groundTrackingMode = trackingPayload.groundType();
    }

    // Log to message bus
    // Format: fanet_rx,<FanetID>,<distance in km>,<RSSI>,<SNR>,<Packet Lat>,<Packet
    // Lon>,<MyLat>,<MyLon>
    if (LOG::FANET_RX && bus_) {
      String fanetRxName = "fanet_rx,";
      String fanetEntry = fanetRxName + String(neighbor.address.asUint(), HEX) + "," +
                          (neighbor.distanceKm.value()) + "," + String(neighbor.rssi) + "," +
                          String(neighbor.snr) + "," + String(lat, 8) + "," + String(lon, 8) + "," +
                          String(gps.location.lat(), 8) + "," + String(gps.location.lng(), 8);
      bus_->receive(CommentMessage(fanetEntry));
    }
  }

  void on_receive_unknown(const etl::imessage& msg) {}
};