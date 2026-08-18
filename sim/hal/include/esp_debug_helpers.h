#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// On device this dumps a backtrace over the console; on the host we print the host backtrace,
// which is far more useful when a fatal error fires inside the emulator.
void esp_backtrace_print(int depth);

#ifdef __cplusplus
}
#endif
