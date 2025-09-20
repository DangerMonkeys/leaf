#pragma once

#include <Arduino.h>

#include "dispatch/message_sink.h"
#include "dispatch/message_types.h"
#include "hardware/configuration.h"

/// @brief Listens for button events on the message bus and then dispatches them to the appropriate
/// UI element.
class ButtonDispatcher : public MessageSink<ButtonDispatcher, ButtonEventMessage> {
 public:
  // MessageSink<ButtonDispatcher, ButtonEventMessage>
  void on_receive(const ButtonEventMessage& msg);
  void on_receive_unknown(const etl::imessage& msg) {}
};

extern ButtonDispatcher buttonDispatcher;
