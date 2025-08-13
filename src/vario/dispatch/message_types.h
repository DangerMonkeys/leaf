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
#include "ui/input/buttons.h"

#define FANET_MAX_FRAME_SIZE 244  // Maximum size of a FANET frame

// NMEAString is 82 characters per standard + 2 for \r\n + 1 for null terminator
constexpr uint8_t MAX_NMEA_SENTENCE_LENGTH = 82 + 2 + 1;

using NMEAString = etl::string<MAX_NMEA_SENTENCE_LENGTH>;

enum MessageType : etl::message_id_t {
  GPS_UPDATE,
  GPS_MESSAGE,
  FANET_PACKET,
  AMBIENT_UPDATE,
  MOTION_UPDATE,
  PRESSURE_UPDATE,
  BUTTON_EVENT,
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
  // Temperature in degrees C
  float temperature;

  // Relative humidity in percent
  float relativeHumidity;

  AmbientUpdate(float temp, float relRH) : temperature(temp), relativeHumidity(relRH) {}
};

/// @brief Update regarding motion
struct MotionUpdate : public etl::message<MOTION_UPDATE> {
  // millis() at which this update was received
  unsigned long t;

  // Whether this update includes orientation information
  bool hasOrientation = false;

  // Orientation quaternion, containing orientation information if hasOrientation is true
  double qx, qy, qz;

  // Whether this update includes acceleration information
  bool hasAcceleration = false;

  // Acceleration vector, containing acceleration information if hasAcceleration is true
  double ax, ay, az;

  MotionUpdate(unsigned long t) : t(t) {};
};

/// @brief Update regarding pressure
struct PressureUpdate : public etl::message<PRESSURE_UPDATE> {
  // millis() at which this update was received
  unsigned long t;

  // Pressure in 100ths of mBar
  int32_t pressure;

  PressureUpdate(unsigned long t, int32_t pressure) : t(t), pressure(pressure) {};
};

/// @brief Update regarding button presses
struct ButtonEvent : public etl::message<BUTTON_EVENT> {
  // Which button triggered event
  Button button;

  // Current state of the button
  ButtonState state;

  uint16_t holdCount;

  ButtonEvent(Button button, ButtonState state, uint16_t holdCount = 0)
      : button(button), state(state), holdCount(holdCount) {}
};
