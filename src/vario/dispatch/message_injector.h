#pragma once

#include <stddef.h>

#include "etl/message_bus.h"

// Parses lines of the bus-log wire format and publishes them onto a message bus.
//
// This is the format BusLogger writes (logging/buslog.cpp), the format sim/play_buslog.py sends
// over UDP, and the format the emulator plays recordings in.  One parser serves all three so a
// recording means the same thing wherever it is replayed:
//
//   A<dt>,<temperature>,<relativeHumidity>       ambient update
//   G<dt>,<nmea sentence>                        GPS sentence
//   M<dt>,<A|a>,<ax>,<ay>,<az>,<Q|q>,<qx>,<qy>,<qz>   motion update
//   P<dt>,<pressure>                             pressure update
//   #<dt>,<text>                                 comment (logged, not published)
//   !<command>                                   control: sensor connect/disconnect, time reset
//
// <dt> is milliseconds since the start of the recording.  Timestamps are rebased onto the current
// clock so a recording played an hour into a session still produces sensible message times.
class MessageInjector {
 public:
  void setBus(etl::imessage_bus* bus) { bus_ = bus; }
  etl::imessage_bus* bus() const { return bus_; }

  // Handles one line (no trailing newline required).  Returns false if the line was not
  // recognised as an injectable message.
  bool handleLine(const char* line, size_t len);

  // Forgets the time origin, so the next timestamped message starts a fresh recording.
  void resetReferenceTime() { tStart_ = 0; }

 private:
  unsigned long adjustedTime(unsigned long dt);

  void onComment(const char* line, size_t len);
  void onCommand(const char* line, size_t len);
  void onAmbientUpdate(const char* line, size_t len);
  void onGPSMessage(const char* line, size_t len);
  void onMotionUpdate(const char* line, size_t len);
  void onPressureUpdate(const char* line, size_t len);

  etl::imessage_bus* bus_ = nullptr;
  unsigned long tStart_ = 0;
};
