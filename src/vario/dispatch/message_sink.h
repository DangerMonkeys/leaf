#pragma once

#include "etl/message_bus.h"

#include "diagnostics/fatal_error.h"

template <typename TDerived, typename... TMessageTypes>
class MessageSink : public etl::message_router<TDerived, TMessageTypes...> {
 public:
  // Subscribe to messages from the specified bus.
  void subscribeTo(etl::imessage_bus* bus) {
    bool success = bus->subscribe(*this);
    if (!success) {
      fatalError("Message bus subscription failed");
    }
  }
};
