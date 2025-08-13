#pragma once

#include "etl/message_bus.h"

#include "dispatch/message_types.h"

class ButtonMonitor : public etl::message_router<ButtonMonitor, ButtonEvent> {
 public:
  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // etl::message_router<ButtonMonitor, ButtonEvent>
  void on_receive(const ButtonEvent& msg);
  void on_receive_unknown(const etl::imessage& msg) {}
};

extern ButtonMonitor buttonMonitor;
