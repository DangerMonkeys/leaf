#pragma once

#include "etl/message_bus.h"

#include "dispatch/message_types.h"

class Ambient : public etl::message_router<Ambient, AmbientUpdate> {
 public:
  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // Get the most recent temperature in degrees Celsius
  float getTemp() { return temperature_; }

  // Get the most recent relative humidity in percent
  float getHumidity() { return relativeHumidity_; }

  // etl::message_router<Ambient, AmbientUpdate>
  void on_receive(const AmbientUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

 private:
  float temperature_;
  float relativeHumidity_;
};

extern Ambient ambient;
