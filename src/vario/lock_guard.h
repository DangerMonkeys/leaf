#pragma once

#include "FreeRTOS.h"

/// @brief Simple FreeRTOS locking guard to lock a mutex in scope
class LockGuard {
 public:
  explicit LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
  }

  ~LockGuard() { xSemaphoreGive(mutex_); }

  // Prevent copying
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  SemaphoreHandle_t mutex_;
};
