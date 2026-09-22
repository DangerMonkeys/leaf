// Sim implementations of the ESP-IDF MCPWM and esp_timer APIs.
//
// Only the speaker driver uses these APIs, so the implementation is deliberately minimal:
//
//   - MCPWM: tracks resolution_hz + period_ticks and calls sim::board().writeTone() whenever
//     the frequency or mute state changes.  No actual PWM signal is generated.
//
//   - esp_timer (periodic): drives the callback synchronously in a tight loop until the callback
//     cancels itself.  For the speaker's smoothing use case this converges in O(log delta) steps
//     and lets the same graduated ramp logic run in the emulator without a background thread or
//     virtual-clock integration.

#include <driver/mcpwm_timer.h>
#include <esp_timer.h>

#include "sim/board.h"

// ------------------------------------------------------------------ internal state

namespace {

  uint32_t s_resolution_hz = 1;  // learned from mcpwm_new_timer
  uint32_t s_period_ticks = 0;   // latest period; 0 = unset
  bool s_running = false;
  bool s_forced_low = true;

  void updateTone() {
    if (!s_running || s_forced_low || s_period_ticks == 0 || s_resolution_hz == 0) {
      sim::board().writeTone(0);
    } else {
      sim::board().writeTone(s_resolution_hz / s_period_ticks);
    }
  }

  struct FakeTimer {
    void (*callback)(void*);
    void* arg;
    bool active;
  };

}  // namespace

// ------------------------------------------------------------------ MCPWM stubs

extern "C" {

esp_err_t mcpwm_new_timer(const mcpwm_timer_config_t* config, mcpwm_timer_handle_t* ret_timer) {
  s_resolution_hz = config->resolution_hz ? config->resolution_hz : 1;
  s_period_ticks = config->period_ticks;
  // Return a non-null sentinel; we track all state globally (one timer per firmware).
  *ret_timer = reinterpret_cast<mcpwm_timer_handle_t>(1);
  return ESP_OK;
}

esp_err_t mcpwm_timer_enable(mcpwm_timer_handle_t) { return ESP_OK; }

esp_err_t mcpwm_timer_disable(mcpwm_timer_handle_t) {
  s_running = false;
  updateTone();
  return ESP_OK;
}

esp_err_t mcpwm_timer_start_stop(mcpwm_timer_handle_t, mcpwm_timer_start_stop_cmd_t command) {
  s_running = (command == MCPWM_TIMER_START_NO_STOP);
  updateTone();
  return ESP_OK;
}

esp_err_t mcpwm_timer_set_period(mcpwm_timer_handle_t, uint32_t period_ticks) {
  s_period_ticks = period_ticks;
  // Only push a tone event when actually playing; avoids noise during init.
  if (s_running && !s_forced_low) updateTone();
  return ESP_OK;
}

esp_err_t mcpwm_new_operator(const mcpwm_operator_config_t*, mcpwm_oper_handle_t* ret_oper) {
  *ret_oper = reinterpret_cast<mcpwm_oper_handle_t>(1);
  return ESP_OK;
}

esp_err_t mcpwm_operator_connect_timer(mcpwm_oper_handle_t, mcpwm_timer_handle_t) { return ESP_OK; }

esp_err_t mcpwm_new_comparator(mcpwm_oper_handle_t, const mcpwm_comparator_config_t*,
                               mcpwm_cmpr_handle_t* ret_cmpr) {
  *ret_cmpr = reinterpret_cast<mcpwm_cmpr_handle_t>(1);
  return ESP_OK;
}

// Compare value is purely for hardware duty-cycle; no-op in the sim.
esp_err_t mcpwm_comparator_set_compare_value(mcpwm_cmpr_handle_t, uint32_t) { return ESP_OK; }

esp_err_t mcpwm_new_generator(mcpwm_oper_handle_t, const mcpwm_generator_config_t*,
                              mcpwm_gen_handle_t* ret_gen) {
  *ret_gen = reinterpret_cast<mcpwm_gen_handle_t>(1);
  return ESP_OK;
}

// Generator action configuration is hardware-specific; no-op in the sim.
esp_err_t mcpwm_generator_set_action_on_timer_event(mcpwm_gen_handle_t,
                                                    mcpwm_gen_timer_event_action_t) {
  return ESP_OK;
}
esp_err_t mcpwm_generator_set_action_on_compare_event(mcpwm_gen_handle_t,
                                                      mcpwm_gen_compare_event_action_t) {
  return ESP_OK;
}

// Force level: 0 = mute output, -1 = release (let timer run).
esp_err_t mcpwm_generator_set_force_level(mcpwm_gen_handle_t, int level, bool) {
  s_forced_low = (level == 0);
  updateTone();
  return ESP_OK;
}

// ------------------------------------------------------------------ esp_timer stubs

esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out_handle) {
  auto* t = new FakeTimer{args->callback, args->arg, false};
  *out_handle = reinterpret_cast<esp_timer_handle_t>(t);
  return ESP_OK;
}

// Drive the callback synchronously until it cancels the timer (e.g. smoothing converged).
// This is safe because the speaker's smoothing loop converges in O(log delta) iterations.
esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle, uint64_t /*period_us*/) {
  auto* t = reinterpret_cast<FakeTimer*>(handle);
  t->active = true;
  while (t->active) {
    t->callback(t->arg);
  }
  return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
  reinterpret_cast<FakeTimer*>(handle)->active = false;
  return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t handle) {
  return reinterpret_cast<FakeTimer*>(handle)->active;
}

}  // extern "C"
