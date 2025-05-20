#pragma once
/*
  Message Types

  This file defines all of the message types that can be passed through
  the ETL Message Bus between modules of the system
*/

#include "TinyGPSPlus.h"
#include "etl/message.h"
#include "fanet/packet.hpp"

#define FANET_MAX_FRAME_SIZE 244  // Maximum size of a FANET frame

enum MessageType {
  GPS_UPDATE,
  FANET_PACKET,
};

/// @brief A GPS update received
struct GpsReading : public etl::message<GPS_UPDATE> {
  GpsReading(TinyGPSPlus reading) : gps(reading) {}
  TinyGPSPlus gps;
};

/// @brief A FANET packet received
struct FanetPacket : public etl::message<FANET_PACKET> {
  FANET::Packet<FANET_MAX_FRAME_SIZE> packet;
  float rssi;
  float snr;

  FanetPacket(FANET::Packet<FANET_MAX_FRAME_SIZE> packet, float rssi, float snr)
      : packet(packet), rssi(rssi), snr(snr) {}
};