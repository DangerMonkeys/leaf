#pragma once

#include "etl/message_bus.h"

#include "dispatch/message_types.h"
#include "logbook/flight.h"

class Bus
    : public Flight,
      public etl::message_router<Bus, AmbientUpdate, GpsMessage, MotionUpdate, PressureUpdate> {
 public:
  // == Flight ==

  bool startFlight() override;
  void end(const FlightStats stats) override;

  const String fileNameSuffix() const override { return "log"; }
  const String desiredFileName() const override;

  // etl::message_router<Bus, AmbientUpdate, GpsMessage>
  void on_receive(const AmbientUpdate& msg);
  void on_receive(const GpsMessage& msg);
  void on_receive(const MotionUpdate& msg);
  void on_receive(const PressureUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  void setBus(etl::imessage_bus* bus) { bus_ = bus; }

 private:
  unsigned long tStart_;
  etl::imessage_bus* bus_ = nullptr;
};
