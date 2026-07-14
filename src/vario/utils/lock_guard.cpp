#include "utils/lock_guard.h"

#include <SD_MMC.h>

#include "diagnostics/fatal_error.h"

LockGuard::LockGuard(SemaphoreHandle_t mutex, TickType_t timeoutTicks, bool fatalOnTimeout)
    : mutex_(mutex) {
  // Try to take the lock out
  if (mutex_ != NULL && xSemaphoreTake(mutex_, timeoutTicks) == pdTRUE) {
    valid_ = true;
    return;
  }

  if (fatalOnTimeout) fatalError("Lock acquisition failed in LockGuard constructor");
}
