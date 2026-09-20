#pragma once
// Minimal mock of the ESP-IDF MCPWM driver headers for the host emulator build.
// All shared types are defined here; mcpwm_oper/cmpr/gen.h just include this file.

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------ handles
struct mcpwm_timer_t;
struct mcpwm_oper_t;
struct mcpwm_cmpr_t;
struct mcpwm_gen_t;

typedef struct mcpwm_timer_t* mcpwm_timer_handle_t;
typedef struct mcpwm_oper_t* mcpwm_oper_handle_t;
typedef struct mcpwm_cmpr_t* mcpwm_cmpr_handle_t;
typedef struct mcpwm_gen_t* mcpwm_gen_handle_t;

// ------------------------------------------------------------------ enumerations
typedef enum { MCPWM_TIMER_CLK_SRC_DEFAULT = 0 } mcpwm_timer_clock_source_t;

typedef enum {
  MCPWM_TIMER_COUNT_MODE_PAUSE = 0,
  MCPWM_TIMER_COUNT_MODE_UP = 1,
  MCPWM_TIMER_COUNT_MODE_DOWN = 2,
  MCPWM_TIMER_COUNT_MODE_UP_DOWN = 3,
} mcpwm_timer_count_mode_t;

typedef enum {
  MCPWM_TIMER_DIRECTION_UP = 0,
  MCPWM_TIMER_DIRECTION_DOWN = 1,
} mcpwm_timer_direction_t;

typedef enum {
  MCPWM_TIMER_EVENT_EMPTY = 0,
  MCPWM_TIMER_EVENT_FULL = 1,
  MCPWM_TIMER_EVENT_INVALID,
} mcpwm_timer_event_t;

typedef enum {
  MCPWM_GEN_ACTION_KEEP = 0,
  MCPWM_GEN_ACTION_LOW = 1,
  MCPWM_GEN_ACTION_HIGH = 2,
  MCPWM_GEN_ACTION_TOGGLE = 3,
} mcpwm_generator_action_t;

typedef enum {
  MCPWM_TIMER_START_NO_STOP = 0,
  MCPWM_TIMER_STOP_EMPTY = 1,
  MCPWM_TIMER_STOP_FULL = 2,
} mcpwm_timer_start_stop_cmd_t;

// ------------------------------------------------------------------ config structs
typedef struct {
  int group_id;
  mcpwm_timer_clock_source_t clk_src;
  uint32_t resolution_hz;
  mcpwm_timer_count_mode_t count_mode;
  uint32_t period_ticks;
  int intr_priority;
  struct {
    uint32_t update_period_on_empty : 1;
    uint32_t update_period_on_sync : 1;
  } flags;
} mcpwm_timer_config_t;

typedef struct {
  int group_id;
  int intr_priority;
  struct {
    uint32_t update_gen_action_on_tez : 1;
    uint32_t update_gen_action_on_tep : 1;
    uint32_t update_gen_action_on_sync : 1;
  } flags;
} mcpwm_operator_config_t;

typedef struct {
  int intr_priority;
  struct {
    uint32_t update_cmp_on_tez : 1;
    uint32_t update_cmp_on_tep : 1;
    uint32_t update_cmp_on_sync : 1;
  } flags;
} mcpwm_comparator_config_t;

typedef struct {
  int gen_gpio_num;
  struct {
    uint32_t invert_pwm : 1;
    uint32_t io_loop_back : 1;
  } flags;
} mcpwm_generator_config_t;

// ------------------------------------------------------------------ event-action structs
typedef struct {
  mcpwm_timer_direction_t direction;
  mcpwm_timer_event_t event;
  mcpwm_generator_action_t action;
} mcpwm_gen_timer_event_action_t;

typedef struct {
  mcpwm_timer_direction_t direction;
  mcpwm_cmpr_handle_t comparator;
  mcpwm_generator_action_t action;
} mcpwm_gen_compare_event_action_t;

#ifdef __cplusplus
}  // extern "C"
#endif

// Compound-literal macros — outside extern "C" so they can use C++ cast syntax cleanly.
// GCC gnu++17 supports compound literals as an extension; this matches the real ESP-IDF macros.
#define MCPWM_GEN_TIMER_EVENT_ACTION(dir, ev, act) \
  ((mcpwm_gen_timer_event_action_t){.direction = (dir), .event = (ev), .action = (act)})
#define MCPWM_GEN_COMPARE_EVENT_ACTION(dir, cmp, act) \
  ((mcpwm_gen_compare_event_action_t){.direction = (dir), .comparator = (cmp), .action = (act)})

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------ function declarations

// Timer
esp_err_t mcpwm_new_timer(const mcpwm_timer_config_t* config, mcpwm_timer_handle_t* ret_timer);
esp_err_t mcpwm_timer_enable(mcpwm_timer_handle_t timer);
esp_err_t mcpwm_timer_start_stop(mcpwm_timer_handle_t timer, mcpwm_timer_start_stop_cmd_t command);
esp_err_t mcpwm_timer_set_period(mcpwm_timer_handle_t timer, uint32_t period_ticks);

// Operator
esp_err_t mcpwm_new_operator(const mcpwm_operator_config_t* config, mcpwm_oper_handle_t* ret_oper);
esp_err_t mcpwm_operator_connect_timer(mcpwm_oper_handle_t oper, mcpwm_timer_handle_t timer);

// Comparator
esp_err_t mcpwm_new_comparator(mcpwm_oper_handle_t oper, const mcpwm_comparator_config_t* config,
                               mcpwm_cmpr_handle_t* ret_cmpr);
esp_err_t mcpwm_comparator_set_compare_value(mcpwm_cmpr_handle_t cmpr, uint32_t cmp_ticks);

// Generator
esp_err_t mcpwm_new_generator(mcpwm_oper_handle_t oper, const mcpwm_generator_config_t* config,
                              mcpwm_gen_handle_t* ret_gen);
esp_err_t mcpwm_generator_set_action_on_timer_event(mcpwm_gen_handle_t gen,
                                                    mcpwm_gen_timer_event_action_t ev_act);
esp_err_t mcpwm_generator_set_action_on_compare_event(mcpwm_gen_handle_t gen,
                                                      mcpwm_gen_compare_event_action_t ev_act);
esp_err_t mcpwm_generator_set_force_level(mcpwm_gen_handle_t gen, int level, bool hold_on);

#ifdef __cplusplus
}
#endif
