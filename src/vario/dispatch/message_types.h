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
#include "hardware/ambient_source.h"

#define FANET_MAX_FRAME_SIZE 244  // Maximum size of a FANET frame
// NMEAString is 82 characters per standard + 2 for \r\n + 1 for null terminator
using NMEAString = etl::string<85>;

enum MessageType : etl::message_id_t {
  GPS_UPDATE = 1,
  GPS_MESSAGE = 2,
  FANET_PACKET = 3,
  AMBIENT_UPDATE = 4,
  MESSAGE_LOGGING_BEGIN = 254,
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

/// @brief Update regarding ambient environment
struct AmbientUpdate : public etl::message<AMBIENT_UPDATE> {
  AmbientUpdateResult updates;

  // Temperature in degrees C
  float temperature;

  // Relative humidity in percent
  float relativeHumidity;

  AmbientUpdate(float temp, float relRH, AmbientUpdateResult updates)
      : temperature(temp), relativeHumidity(relRH), updates(updates) {}
};
