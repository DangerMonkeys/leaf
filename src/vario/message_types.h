#pragma once
#include "TinyGPSPlus.h"
#include "etl/message.h"
#include "fanetPacket.h"

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
  Fanet::Packet packet;
  float rssi;
  float snr;

  FanetPacket(Fanet::Packet packet, float rssi, float snr) : packet(packet), rssi(rssi), snr(snr) {}
};