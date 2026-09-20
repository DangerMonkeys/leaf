#pragma once
// Minimal mock of the ESP-IDF esp_timer API for the host emulator build.
// The real periodic-timer machinery is replaced by a synchronous convergence loop in
// sim/hal/src/mcpwm_sim.cpp so that speaker smoothing still reaches its target frequency
// without requiring a background thread or virtual-clock integration.

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESP_TIMER_TASK = 0,
  ESP_TIMER_ISR = 1,
} esp_timer_dispatch_method_t;

typedef struct {
  void (*callback)(void* arg);
  void* arg;
  esp_timer_dispatch_method_t dispatch_method;
  const char* name;
  bool skip_unhandled_events;
} esp_timer_create_args_t;

struct esp_timer;
typedef struct esp_timer* esp_timer_handle_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t* create_args,
                           esp_timer_handle_t* out_handle);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
bool esp_timer_is_active(esp_timer_handle_t timer);

#ifdef __cplusplus
}
#endif
