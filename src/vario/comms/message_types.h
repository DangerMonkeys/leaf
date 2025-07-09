#pragma once
/*
  Message Types

  This file defines all of the message types that can be passed through
  the ETL Message Bus between modules of the system
*/

#include "TinyGPSPlus.h"
#include "etl/message.h"
#include "etl/string.h"
#include "fanet/packet.hpp"

#define FANET_MAX_FRAME_SIZE 244  // Maximum size of a FANET frame
// NMEAString is 82 characters per standard + 2 for \r\n + 1 for null terminator
using NMEAString = etl::string<85>;

enum MessageType {
  GPS_UPDATE,
  GPS_MESSAGE,
  FANET_PACKET,
};

/// @brief A GPS update received
struct GpsReading : public etl::message<GPS_UPDATE> {
  GpsReading(TinyGPSPlus reading) : gps(reading) {}
  TinyGPSPlus gps;
};

struct GpsMessage : public etl::message<GPS_MESSAGE> {
  // A GPS message that is not a reading, but a raw NMEA sentence
  // This is useful for when parts of the application need to log or
  // process raw NMEA sentences
  NMEAString nmea;

  GpsMessage(NMEAString nmea) : nmea(nmea) {}
};

/// @brief A FANET packet received
struct FanetPacket : public etl::message<FANET_PACKET> {
  FANET::Packet<FANET_MAX_FRAME_SIZE> packet;
  float rssi;
  float snr;

  FanetPacket(FANET::Packet<FANET_MAX_FRAME_SIZE> packet, float rssi, float snr)
      : packet(packet), rssi(rssi), snr(snr) {}
};