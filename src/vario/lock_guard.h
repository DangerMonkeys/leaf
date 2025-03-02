#pragma once

#include "FreeRTOS.h"

/// @brief Simple FreeRTOS locking guard to lock a mutex in scope
class LockGuard {
 public:
  explicit LockGuard(SemaphoreHandle_t mutex);

  ~LockGuard() { xSemaphoreGive(mutex_); }

  // Allow this to be used in if statements
  explicit operator bool() const { return true; }

  // Prevent copying
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  SemaphoreHandle_t mutex_;
};
