#include "utils/lock_guard.h"

#include <SD_MMC.h>

#include "diagnostics/fatal_error.h"

LockGuard::LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
  // Try to take the lock out
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10000)) == pdTRUE) return;

  fatalError("Lock acquisition failed in LockGuard constructor");
}
