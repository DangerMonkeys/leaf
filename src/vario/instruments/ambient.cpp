#include "instruments/ambient.h"

#include "etl/message_bus.h"

#include "dispatch/message_types.h"
#include "hardware/aht20.h"
#include "hardware/ambient_source.h"

void Ambient::on_receive(const AmbientUpdate& msg) {
  if (FLAG_SET(msg.updates, AmbientUpdateResult::TemperatureReady)) {
    temperature_ = msg.temperature;
  }
  if (FLAG_SET(msg.updates, AmbientUpdateResult::RelativeHumidityReady)) {
    relativeHumidity_ = msg.relativeHumidity;
  }
}

Ambient ambient;
