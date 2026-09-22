#include "ui/audio/speaker_driver.h"

#include <Arduino.h>

#include <atomic>
#include <mutex>

#include <driver/mcpwm_cmpr.h>
#include <driver/mcpwm_gen.h>
#include <driver/mcpwm_oper.h>
#include <driver/mcpwm_timer.h>
#include "diagnostics/fatal_error.h"
#include "hardware/configuration.h"

namespace speaker_driver {
  namespace {
    // Keep resolution low enough that the lowest tones fit within MCPWM peak-tick limits.
    // 1 MHz supports down to ~15 Hz with a 16-bit peak counter while keeping 1 Hz granularity.
    constexpr uint32_t MCPWM_RESOLUTION_HZ = 1000000;
    constexpr uint32_t BOOTSTRAP_FREQUENCY_HZ = 1000;

    mcpwm_timer_handle_t pwmTimer = nullptr;
    mcpwm_oper_handle_t pwmOperator = nullptr;
    mcpwm_cmpr_handle_t pwmComparator = nullptr;
    mcpwm_gen_handle_t pwmGenerator = nullptr;

    std::atomic<uint32_t> speakerActiveFrequency{0};
    std::atomic<bool> timerRunning{false};
    std::atomic<bool> timerEnabled{false};

    // Serializes mcpwm_timer_set_period + mcpwm_comparator_set_compare_value so the two-step
    // update is never interleaved with a concurrent applyFrequency call from another task.
    std::mutex applyMutex;

    void checkEsp(const char* step, esp_err_t err) {
      if (err != ESP_OK) {
        fatalError("speaker_driver %s failed: %d", step, static_cast<int>(err));
      }
    }

    uint32_t periodTicksForFrequency(uint32_t frequency) {
      if (frequency == 0) return MCPWM_RESOLUTION_HZ / BOOTSTRAP_FREQUENCY_HZ;
      uint32_t ticks = MCPWM_RESOLUTION_HZ / frequency;
      if (ticks < 2) ticks = 2;
      return ticks;
    }

    void applyFrequency(uint32_t frequency) {
      if (frequency == 0) return;

      const uint32_t periodTicks = periodTicksForFrequency(frequency);
      esp_err_t setPeriodErr = ESP_OK;
      esp_err_t setComparatorErr = ESP_OK;
      {
        std::lock_guard<std::mutex> lock(applyMutex);
        setPeriodErr = mcpwm_timer_set_period(pwmTimer, periodTicks);
        if (setPeriodErr == ESP_OK) {
          setComparatorErr = mcpwm_comparator_set_compare_value(pwmComparator, periodTicks / 2);
        }
      }

      // Call fatal handling only after releasing applyMutex. fatalError() drives speaker playback,
      // so reporting from inside the lock can deadlock if the driver is re-entered.
      checkEsp("timer_set_period", setPeriodErr);
      checkEsp("comparator_set", setComparatorErr);
      speakerActiveFrequency.store(frequency, std::memory_order_relaxed);
    }
  }  // namespace

  void init() {
    if (pwmTimer != nullptr) return;

    const mcpwm_timer_config_t timerConfig = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_RESOLUTION_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = periodTicksForFrequency(BOOTSTRAP_FREQUENCY_HZ),
        .intr_priority = 0,
        .flags =
            {
                .update_period_on_empty = 1,
                .update_period_on_sync = 0,
            },
    };
    checkEsp("new_timer", mcpwm_new_timer(&timerConfig, &pwmTimer));

    const mcpwm_operator_config_t operatorConfig = {
        .group_id = 0,
        .intr_priority = 0,
        .flags = {},
    };
    checkEsp("new_operator", mcpwm_new_operator(&operatorConfig, &pwmOperator));
    checkEsp("connect_timer", mcpwm_operator_connect_timer(pwmOperator, pwmTimer));

    const mcpwm_comparator_config_t comparatorConfig = {
        .intr_priority = 0,
        .flags =
            {
                .update_cmp_on_tez = 1,
                .update_cmp_on_tep = 0,
                .update_cmp_on_sync = 0,
            },
    };
    checkEsp("new_comparator",
             mcpwm_new_comparator(pwmOperator, &comparatorConfig, &pwmComparator));

    const mcpwm_generator_config_t generatorConfig = {
        .gen_gpio_num = SPEAKER_PIN,
        .flags = {},
    };
    checkEsp("new_generator", mcpwm_new_generator(pwmOperator, &generatorConfig, &pwmGenerator));

    checkEsp("gen_action_empty",
             mcpwm_generator_set_action_on_timer_event(
                 pwmGenerator,
                 MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY,
                                              MCPWM_GEN_ACTION_HIGH)));
    checkEsp("gen_action_compare",
             mcpwm_generator_set_action_on_compare_event(
                 pwmGenerator, MCPWM_GEN_COMPARE_EVENT_ACTION(
                                   MCPWM_TIMER_DIRECTION_UP, pwmComparator, MCPWM_GEN_ACTION_LOW)));

    applyFrequency(BOOTSTRAP_FREQUENCY_HZ);
    checkEsp("force_low", mcpwm_generator_set_force_level(pwmGenerator, 0, true));
  }

  void playTone(uint32_t frequency) {
    if (pwmTimer == nullptr) return;

    if (frequency == 0) {
      if (timerRunning.load(std::memory_order_relaxed)) {
        checkEsp("timer_stop", mcpwm_timer_start_stop(pwmTimer, MCPWM_TIMER_STOP_EMPTY));
        timerRunning.store(false, std::memory_order_relaxed);
      }

      if (timerEnabled.load(std::memory_order_relaxed)) {
        checkEsp("timer_disable", mcpwm_timer_disable(pwmTimer));
        timerEnabled.store(false, std::memory_order_relaxed);
      }

      speakerActiveFrequency.store(0, std::memory_order_relaxed);
      checkEsp("force_low", mcpwm_generator_set_force_level(pwmGenerator, 0, true));
      return;
    }

    applyFrequency(frequency);

    if (!timerEnabled.load(std::memory_order_relaxed)) {
      checkEsp("timer_enable", mcpwm_timer_enable(pwmTimer));
      timerEnabled.store(true, std::memory_order_relaxed);
    }

    checkEsp("force_release", mcpwm_generator_set_force_level(pwmGenerator, -1, true));
    if (!timerRunning.load(std::memory_order_relaxed)) {
      checkEsp("timer_start", mcpwm_timer_start_stop(pwmTimer, MCPWM_TIMER_START_NO_STOP));
      timerRunning.store(true, std::memory_order_relaxed);
    }
  }
}  // namespace speaker_driver