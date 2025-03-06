#pragma once

#include "etl/message_bus.h"
#include "lock_guard.h"

/// @brief Thread safe MessageBus
/// @tparam MAX_ROUTERS_
template <uint_least8_t MAX_ROUTERS_>
class MessageBus : public etl::message_bus<MAX_ROUTERS_> {
 public:
  /// @brief Receives a message and processes it in a thread-safe manner.
  MessageBus() : etl::message_bus<MAX_ROUTERS_>() { mutex = xSemaphoreCreateMutex(); }
  ~MessageBus() override { vSemaphoreDelete(mutex); }

  virtual void receive(const etl::imessage& message) override;

 private:
  SemaphoreHandle_t mutex;
};

template <uint_least8_t MAX_ROUTERS_>
inline void MessageBus<MAX_ROUTERS_>::receive(const etl::imessage& message) {
  LockGuard lockGuard(mutex);
  etl::message_bus<MAX_ROUTERS_>::receive(message);
}
