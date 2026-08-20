#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Non-volatile storage lives in a JSON file under the emulator's state directory; erasing it
// clears that file, which is what the firmware's "reset all settings" path expects.
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

#ifdef __cplusplus
}
#endif
