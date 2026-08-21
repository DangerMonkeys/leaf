#pragma once

// Minimal stand-in for ESP-IDF's sleep API. The emulator's clock is virtual and there is no real
// low-power hardware to program, so these report success without doing anything real; see
// esp_light_sleep_start's definition in sim/hal/src/esp.cpp for how it still keeps timing right.

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESP_EXT1_WAKEUP_ALL_LOW,
  ESP_EXT1_WAKEUP_ANY_HIGH,
} esp_sleep_ext1_wakeup_mode_t;

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_sleep_enable_ext0_wakeup(int gpio_num, int level);
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode);
esp_err_t esp_light_sleep_start(void);

#ifdef __cplusplus
}
#endif
