#include "ui/audio/speaker_driver.h"

#include <Arduino.h>

#include <atomic>
#include <mutex>

#include <driver/mcpwm_cmpr.h>
#include <driver/mcpwm_gen.h>
#include <driver/mcpwm_oper.h>
#include <driver/mcpwm_timer.h>
#include <esp_timer.h>
#include "diagnostics/fatal_error.h"
#include "hardware/configuration.h"

namespace speaker_driver {
  namespace {
    // Keep resolution low enough that the lowest tones fit within MCPWM peak-tick limits.
    // 1 MHz supports down to ~15 Hz with a 16-bit peak counter while keeping 1 Hz granularity.
    constexpr uint32_t MCPWM_RESOLUTION_HZ = 1000000;
    constexpr uint32_t DEFAULT_FREQUENCY_HZ = 1000;
    constexpr int64_t SMOOTH_STEP_INTERVAL_US = 1000;
    // Smoothing target: limit each 1 ms step to at most 0.5% of current frequency.
    // This keeps large jumps perceptually smoother while still converging quickly.
    constexpr uint32_t SMOOTH_MAX_PPM_PER_STEP = 5000;

    mcpwm_timer_handle_t pwmTimer = nullptr;
    mcpwm_oper_handle_t pwmOperator = nullptr;
    mcpwm_cmpr_handle_t pwmComparator = nullptr;
    mcpwm_gen_handle_t pwmGenerator = nullptr;
    esp_timer_handle_t smoothTimer = nullptr;

    std::atomic<uint32_t> speakerRequestedFrequency{0};
    std::atomic<uint32_t> speakerActiveFrequency{0};
    std::atomic<bool> timerRunning{false};
    std::atomic<bool> smoothActive{false};

    // Serializes mcpwm_timer_set_period + mcpwm_comparator_set_compare_value so the two-step
    // update is never interleaved with a concurrent applyFrequency call from another task
    // (e.g. the esp_timer smooth callback running on a different core).
    std::mutex applyMutex;

    void checkEsp(const char* step, esp_err_t err) {
      if (err != ESP_OK) {
        fatalError("speaker_driver %s failed: %d", step, static_cast<int>(err));
      }
    }

    uint32_t periodTicksForFrequency(uint32_t frequency) {
      if (frequency == 0) return MCPWM_RESOLUTION_HZ / DEFAULT_FREQUENCY_HZ;
      uint32_t ticks = MCPWM_RESOLUTION_HZ / frequency;
      if (ticks < 2) ticks = 2;
      return ticks;
    }

    uint32_t nextActiveFrequency(uint32_t activeFrequency, uint32_t requestedFrequency) {
      if (requestedFrequency == activeFrequency) return activeFrequency;

      uint32_t step = (activeFrequency * SMOOTH_MAX_PPM_PER_STEP) / 1000000U;
      if (step < 1) step = 1;

      if (requestedFrequency > activeFrequency) {
        const uint32_t delta = requestedFrequency - activeFrequency;
        if (step > delta) step = delta;
        const uint32_t next = activeFrequency + step;
        return next > requestedFrequency ? requestedFrequency : next;
      }

      const uint32_t delta = activeFrequency - requestedFrequency;
      if (step > delta) step = delta;
      return activeFrequency > requestedFrequency + step ? activeFrequency - step
                                                         : requestedFrequency;
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

    void stopSmoothTimer() {
      if (smoothTimer == nullptr) return;
      if (esp_timer_is_active(smoothTimer))
        checkEsp("smooth_timer_stop", esp_timer_stop(smoothTimer));
    }

    void ensureSmoothTimerStarted() {
      if (smoothTimer == nullptr) return;
      if (!esp_timer_is_active(smoothTimer)) {
        checkEsp("smooth_timer_start",
                 esp_timer_start_periodic(smoothTimer, SMOOTH_STEP_INTERVAL_US));
      }
    }

    void onSmoothStep(void*) {
      if (!timerRunning.load(std::memory_order_relaxed) ||
          !smoothActive.load(std::memory_order_relaxed)) {
        stopSmoothTimer();
        return;
      }

      const uint32_t requested = speakerRequestedFrequency.load(std::memory_order_relaxed);
      const uint32_t active = speakerActiveFrequency.load(std::memory_order_relaxed);

      if (requested == 0 || active == 0) {
        smoothActive.store(false, std::memory_order_relaxed);
        stopSmoothTimer();
        return;
      }

      if (requested == active) {
        smoothActive.store(false, std::memory_order_relaxed);
        stopSmoothTimer();
        return;
      }

      const uint32_t next = nextActiveFrequency(active, requested);
      applyFrequency(next);
    }
  }  // namespace

  void init() {
    if (pwmTimer != nullptr) return;

    const mcpwm_timer_config_t timerConfig = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_RESOLUTION_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = periodTicksForFrequency(DEFAULT_FREQUENCY_HZ),
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

    const esp_timer_create_args_t smoothTimerConfig = {
        .callback = &onSmoothStep,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "speaker_smooth",
        .skip_unhandled_events = true,
    };
    checkEsp("smooth_timer_create", esp_timer_create(&smoothTimerConfig, &smoothTimer));

    applyFrequency(DEFAULT_FREQUENCY_HZ);
    checkEsp("force_low", mcpwm_generator_set_force_level(pwmGenerator, 0, true));
    checkEsp("timer_enable", mcpwm_timer_enable(pwmTimer));
  }

  void playTone(uint32_t frequency, bool smoothTransition) {
    if (pwmTimer == nullptr) return;

    speakerRequestedFrequency.store(frequency, std::memory_order_relaxed);

    if (frequency == 0) {
      smoothActive.store(false, std::memory_order_relaxed);
      stopSmoothTimer();

      if (timerRunning.load(std::memory_order_relaxed)) {
        checkEsp("timer_stop", mcpwm_timer_start_stop(pwmTimer, MCPWM_TIMER_STOP_EMPTY));
        timerRunning.store(false, std::memory_order_relaxed);
      }

      speakerActiveFrequency.store(0, std::memory_order_relaxed);
      checkEsp("force_low", mcpwm_generator_set_force_level(pwmGenerator, 0, true));
      return;
    }

    const uint32_t active = speakerActiveFrequency.load(std::memory_order_relaxed);
    const bool canSmooth = smoothTransition && active != 0;

    if (canSmooth) {
      smoothActive.store(true, std::memory_order_relaxed);
      ensureSmoothTimerStarted();
    } else {
      smoothActive.store(false, std::memory_order_relaxed);
      stopSmoothTimer();
      applyFrequency(frequency);
    }

    checkEsp("force_release", mcpwm_generator_set_force_level(pwmGenerator, -1, true));
    if (!timerRunning.load(std::memory_order_relaxed)) {
      checkEsp("timer_start", mcpwm_timer_start_stop(pwmTimer, MCPWM_TIMER_START_NO_STOP));
      timerRunning.store(true, std::memory_order_relaxed);
    }
  }
}  // namespace speaker_driver