#pragma once

#include "dispatch/message_sink.h"
#include "dispatch/message_types.h"

class ButtonMonitor : public MessageSink<ButtonMonitor, ButtonEventMessage> {
 public:
  // MessageSink<ButtonMonitor, ButtonEventMessage>
  void on_receive(const ButtonEventMessage& msg);
  void on_receive_unknown(const etl::imessage& msg) {}
};

extern ButtonMonitor buttonMonitor;
