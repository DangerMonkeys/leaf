#include "instruments/ambient.h"

#include "etl/message_bus.h"

#include "diagnostics/fatal_error.h"
#include "dispatch/message_types.h"

namespace {
  constexpr unsigned long MAXIMUM_FRESH_DURATION_MS = 15000;
}

void Ambient::on_receive(const AmbientUpdate& msg) {
  temperature_ = msg.temperature;
  relativeHumidity_ = msg.relativeHumidity;
  lastMeasurement_ = millis();
}

Ambient::State Ambient::state() const {
  if (lastMeasurement_ == 0) {
    return State::NoData;
  } else if (millis() - lastMeasurement_ > MAXIMUM_FRESH_DURATION_MS) {
    return State::Stale;
  } else {
    return State::Ready;
  }
}

void Ambient::onUnexpectedState(const char* action, State actual) const {
  if (actual == State::NoData) {
    fatalError("%s without data", action);
  } else if (actual == State::Ready) {
    fatalError("%s while ready", action);
  } else if (actual == State::Stale) {
    fatalError("%s with %lums stale data", action, millis() - lastMeasurement_);
  } else {
    fatalError("%s in unknown state %d", action, actual);
  }
}

float Ambient::temp() const {
  if (state() == State::NoData) {
    return 0.0f;
  }
  return temperature_;
}

float Ambient::humidity() const {
  if (state() == State::NoData) {
    return 0.0f;
  }
  return relativeHumidity_;
}

Ambient ambient;
