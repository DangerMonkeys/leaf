#include "lock_guard.h"
#include <SD_MMC.h>

LockGuard::LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
  // Try to take the lock out
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10000)) == pdTRUE) return;

  assert(0);  // Create a core dump if the lock fails
}