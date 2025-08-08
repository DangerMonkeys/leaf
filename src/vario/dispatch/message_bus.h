#pragma once

#include "etl/message_bus.h"

#include "diagnostics/fatal_error.h"

// How much time to block waiting for the message bus mutex (protecting against concurrent
// cross-threaded messaging) to become available
#define MESSAGE_BUS_BLOCKING_TIME pdMS_TO_TICKS(3000)

/// @brief Thread-safe MessageBus
/// @details Message bus handlers will only be called by one thread at a time: if a handler is
/// active on one thread, no handler will be called in another thread until the first thread
/// finishes its handlers.
/// @tparam MAX_ROUTERS_
template <uint_least8_t MAX_ROUTERS_>
class MessageBus : public etl::message_bus<MAX_ROUTERS_> {
 public:
  MessageBus() : etl::message_bus<MAX_ROUTERS_>() { mutex_ = xSemaphoreCreateRecursiveMutex(); }
  ~MessageBus() override { vSemaphoreDelete(mutex_); }

  virtual void receive(const etl::imessage& message) override;

 private:
  SemaphoreHandle_t mutex_;

  /// @brief Locked view of MessageBus
  struct Locked {
    Locked(SemaphoreHandle_t mutex, etl::message_bus<MAX_ROUTERS_>& bus)
        : mutex_(mutex),
          bus_(bus),
          valid_(xSemaphoreTakeRecursive(mutex_, MESSAGE_BUS_BLOCKING_TIME) == pdTRUE) {}

    ~Locked() {
      if (valid_) {
        xSemaphoreGiveRecursive(mutex_);
      }
    }

    inline bool valid() { return valid_; }

    void receive(const etl::imessage& message) {
      if (valid_) {
        bus_.etl::message_bus<MAX_ROUTERS_>::receive(message);
      }
    }

   private:
    SemaphoreHandle_t mutex_;
    etl::message_bus<MAX_ROUTERS_>& bus_;
    const bool valid_;
  };

  Locked lock() { return Locked(mutex_, static_cast<etl::message_bus<MAX_ROUTERS_>&>(*this)); }
};

template <uint_least8_t MAX_ROUTERS_>
inline void MessageBus<MAX_ROUTERS_>::receive(const etl::imessage& message) {
  Locked locked = lock();
  if (!locked.valid()) {
    fatalError("Lock acquisition failed in MessageBus::receive");
  } else {
    locked.receive(message);
  }
}
