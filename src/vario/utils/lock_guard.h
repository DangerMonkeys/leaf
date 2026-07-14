#pragma once

#include "FreeRTOS.h"

/// @brief Simple FreeRTOS locking guard to lock a mutex in scope
class LockGuard {
 public:
  explicit LockGuard(SemaphoreHandle_t mutex, TickType_t timeoutTicks = pdMS_TO_TICKS(10000),
                     bool fatalOnTimeout = true);

  ~LockGuard() {
    if (valid_) xSemaphoreGive(mutex_);
  }

  // Allow this to be used in if statements
  explicit operator bool() const { return valid_; }

  // Prevent copying
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  SemaphoreHandle_t mutex_;
  bool valid_ = false;
};
