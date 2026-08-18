// ESP-IDF system/error/heap/MAC stand-ins.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
  ESP_RST_USB,
  ESP_RST_JTAG,
  ESP_RST_EFUSE,
  ESP_RST_PWR_GLITCH,
  ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;

esp_reset_reason_t esp_reset_reason(void);
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
void esp_restart(void);

// Sleep entry points the charging loop uses.  In the emulator these advance the virtual clock
// instead of powering anything down.
typedef int esp_sleep_source_t;
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t timeInUs);
esp_err_t esp_sleep_enable_ext0_wakeup(int gpioNum, int level);
esp_err_t esp_light_sleep_start(void);

#ifdef __cplusplus
}
#endif
