#pragma once

#include "etl/message_bus.h"

class IMessageSource {
 public:
  // Publish messages to the specified message bus.
  virtual void publishTo(etl::imessage_bus* bus) = 0;

  // Do not publish messages to message bus any more.
  virtual void stopPublishing() = 0;

  virtual ~IMessageSource() = default;  // Always provide a virtual destructor
};
