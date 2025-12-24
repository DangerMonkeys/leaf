#pragma once

#include "etl/message_bus.h"

#include "dispatch/message_types.h"
#include "logbook/flight.h"

// Logger that records messages sent to the message bus
class BusLogger : public etl::message_router<BusLogger, AmbientUpdate, CommentMessage, GpsMessage,
                                             MotionUpdate, PressureUpdate> {
 public:
  void setBus(etl::imessage_bus* bus) { bus_ = bus; }
  bool startLog();
  bool isLogging() { return file_; }
  void endLog();

  // etl::message_router<Bus, ...>
  void on_receive(const AmbientUpdate& msg);
  void on_receive(const CommentMessage& msg);
  void on_receive(const GpsMessage& msg);
  void on_receive(const MotionUpdate& msg);
  void on_receive(const PressureUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

 private:
  String desiredFileName() const;

  File file_;
  unsigned long tStart_;
  etl::imessage_bus* bus_ = nullptr;
  TimerHandle_t statTimer_;  // Timer to write periodic stats to
  // Callback function for writing periodic stats to the bus log.
  static void statsCallback(TimerHandle_t x);
};

extern BusLogger busLog;
