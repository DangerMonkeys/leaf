#pragma once

#include <stdint.h>

namespace storage_warning {
  // One read-only check per transition into operating mode. Never polled while flying.
  void checkAtStartup();
#ifdef LEAF_USB_BENCH_CONSOLE
  uint32_t lastCheckMicros();
#endif
}  // namespace storage_warning
