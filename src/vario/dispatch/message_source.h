#pragma once

#include "etl/message_bus.h"

class IMessageSource {
 public:
  // Attach this object to the specified message bus and this object will provide messages to the
  // specified message bus.
  virtual void attach(etl::imessage_bus* bus) = 0;

  virtual ~IMessageSource() = default;  // Always provide a virtual destructor
};
