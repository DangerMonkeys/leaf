#include "instruments/ambient.h"

#include "etl/message_bus.h"

#include "dispatch/message_types.h"

void Ambient::on_receive(const AmbientUpdate& msg) {
  temperature_ = msg.temperature;
  relativeHumidity_ = msg.relativeHumidity;
}

Ambient ambient;
