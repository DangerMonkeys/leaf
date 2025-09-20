#pragma once

#include "dispatch/message_sink.h"
#include "dispatch/message_types.h"
#include "utils/state_assert_mixin.h"

class Ambient : public MessageSink<Ambient, AmbientUpdate>, private StateAssertMixin<Ambient> {
 public:
  enum class State : uint8_t { NoData, Ready, Stale };

  State state() const;

  // Get the most recent temperature in degrees Celsius
  float temp() const;

  // Get the most recent relative humidity in percent
  float humidity() const;

  // MessageSink<Ambient, AmbientUpdate>
  void on_receive(const AmbientUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

 private:
  void onUnexpectedState(const char* action, State actual) const;
  friend struct StateAssertMixin<Ambient>;

  float temperature_;
  float relativeHumidity_;
  unsigned long lastMeasurement_ = 0;
};

extern Ambient ambient;
