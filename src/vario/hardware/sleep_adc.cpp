#include "hardware/sleep_adc.h"

#include <Arduino.h>

#ifdef LEAF_SIM
#include "sim/clock.h"
#else
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_sleep.h>
#include <soc/rtc.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/sens_reg.h>
#include <soc/soc.h>
#include <ulp.h>
#include <ulp_adc.h>
#endif

namespace sleep_adc {
  namespace {
    constexpr uint16_t SAMPLE_COUNT = 8;
    uint16_t sampleCount = SAMPLE_COUNT;
    constexpr uint32_t MIN_SLEEP_US = 4000;
    uint32_t sampleDelayUs = 0;
    uint16_t settleCycles = 2000;
    uint32_t burstGuardUs = 1000;
    esp_err_t sampleError = ESP_OK;
    bool prepared = false;

#ifdef LEAF_SIM
    uint8_t samplePin = 0;
    uint64_t sampleStartedUs = 0;
#else
    // The framework reserves 512 bytes of RTC slow memory for the ULP.
    constexpr uint32_t RESULT_WORD = 126;
    constexpr uint32_t COMPLETE_WORD = 127;
    constexpr uint32_t CONVERSIONS_WORD = RESULT_WORD - BURST_CONVERSIONS;
    static_assert(CONFIG_ULP_COPROC_RESERVE_MEM >= 512, "ULP ADC needs 512 RTC bytes");
    adc_oneshot_unit_handle_t cpuAdc = nullptr;
    adc_cali_handle_t calibration = nullptr;
    bool ulpAdc = false;
    bool rtcPeriphHeld = false;
    uint16_t configuredChannels = 0;

    esp_err_t initializeCpuAdc() {
      if (cpuAdc) return ESP_OK;
      const adc_oneshot_unit_init_cfg_t config = {.unit_id = ADC_UNIT_1,
                                                  .ulp_mode = ADC_ULP_MODE_DISABLE};
      const esp_err_t error = adc_oneshot_new_unit(&config, &cpuAdc);
      configuredChannels = 0;
      return error;
    }

    esp_err_t initializeCalibration() {
      if (calibration) return ESP_OK;
      const adc_cali_curve_fitting_config_t config = {
          .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
      return adc_cali_create_scheme_curve_fitting(&config, &calibration);
    }

    esp_err_t channelForPin(uint8_t pin, adc_channel_t& channel) {
      adc_unit_t unit;
      const esp_err_t error = adc_oneshot_io_to_channel(pin, &unit, &channel);
      if (error != ESP_OK) return error;
      return unit == ADC_UNIT_1 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t loadSampler(adc_channel_t channel) {
      const uint32_t pad = static_cast<uint32_t>(channel);
      constexpr uint16_t ACQUISITION_GAP_CYCLES = 400;
      // Stop the timer in the program itself: this is a single burst, followed by HALT.
      // Eight 12-bit samples fit in the ULP's 16-bit accumulator.
      const ulp_insn_t setup[] = {
          I_END(),
          // IDF 5.3.2 misc_modules_sleep_prepare() forces SAR off for
          // light sleep, including ULP ADC mode. Restore clock and
          // power here, after sleep entry, for this burst only.
          I_WR_REG_BIT(SENS_SAR_PERI_CLK_GATE_CONF_REG, SENS_SARADC_CLK_EN_S, 1),
          I_WR_REG(SENS_SAR_POWER_XPD_SAR_REG, SENS_FORCE_XPD_SAR_S, SENS_FORCE_XPD_SAR_S + 1, 3),
          // Match CPU acquisition: select the analog input before starting conversion,
          // and hold it selected through settling and all eight samples. Starting each
          // conversion with a fresh input selection can disturb the unbuffered ISET node.
          I_WR_REG(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_S, SENS_SAR1_EN_PAD_S + 9, 0),
          I_WR_REG_BIT(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_S + pad, 1),
          I_WR_REG_BIT(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_FORCE_S, 1),
          I_DELAY(settleCycles), I_ADC(R0, 0, pad)};
      const ulp_insn_t burst[] = {
          I_MOVI(R3, CONVERSIONS_WORD), I_ST(R0, R3, 0),  // Exclude first from burst average.
          // Leave time for an unbuffered sense input to recover between conversions.
          I_DELAY(ACQUISITION_GAP_CYCLES), I_MOVI(R2, 0), I_ADC(R0, 0, pad), I_ST(R0, R3, 1),
          I_ADDR(R2, R2, R0), I_DELAY(ACQUISITION_GAP_CYCLES), I_ADC(R0, 0, pad), I_ST(R0, R3, 2),
          I_ADDR(R2, R2, R0), I_DELAY(ACQUISITION_GAP_CYCLES), I_ADC(R0, 0, pad), I_ST(R0, R3, 3),
          I_ADDR(R2, R2, R0), I_DELAY(ACQUISITION_GAP_CYCLES), I_ADC(R0, 0, pad), I_ST(R0, R3, 4),
          I_ADDR(R2, R2, R0), I_DELAY(ACQUISITION_GAP_CYCLES), I_ADC(R0, 0, pad), I_ST(R0, R3, 5),
          I_ADDR(R2, R2, R0), I_DELAY(ACQUISITION_GAP_CYCLES), I_ADC(R0, 0, pad), I_ST(R0, R3, 6),
          I_ADDR(R2, R2, R0), I_DELAY(ACQUISITION_GAP_CYCLES), I_ADC(R0, 0, pad), I_ST(R0, R3, 7),
          I_ADDR(R2, R2, R0), I_DELAY(ACQUISITION_GAP_CYCLES), I_ADC(R0, 0, pad), I_ST(R0, R3, 8),
          I_ADDR(R2, R2, R0)};
      const ulp_insn_t powerDown[] = {
          I_WR_REG(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_S, SENS_SAR1_EN_PAD_S + 9, 0),
          I_WR_REG_BIT(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_FORCE_S, 1),
          I_WR_REG(SENS_SAR_POWER_XPD_SAR_REG, SENS_FORCE_XPD_SAR_S, SENS_FORCE_XPD_SAR_S + 1, 2),
          I_WR_REG_BIT(SENS_SAR_PERI_CLK_GATE_CONF_REG, SENS_SARADC_CLK_EN_S, 0)};
      const ulp_insn_t singleResult[] = {I_MOVI(R3, CONVERSIONS_WORD), I_ST(R0, R3, 0),
                                         I_MOVR(R2, R0)};
      const ulp_insn_t complete[] = {I_MOVI(R1, RESULT_WORD), I_ST(R2, R1, 0),
                                     I_MOVI(R0, sampleCount), I_ST(R0, R1, 1), I_HALT()};
      static_assert((sizeof(setup) + sizeof(burst) + sizeof(powerDown) + sizeof(singleResult) +
                     sizeof(complete)) /
                            sizeof(ulp_insn_t) <=
                        CONVERSIONS_WORD,
                    "ULP program overlaps burst results");
      ulp_insn_t program[CONVERSIONS_WORD];
      size_t size = 0;
      auto append = [&](const auto& instructions) {
        for (const auto& instruction : instructions) program[size++] = instruction;
      };
      append(setup);
      if (sampleCount != 1) append(burst);
      append(powerDown);  // Single powers off immediately after its only ADC instruction.
      if (sampleCount == 1) append(singleResult);
      append(complete);
      return ulp_process_macros_and_load(0, program, &size);
    }

    void stopSampler() {
      ulp_timer_stop();
      // A button may wake the CPU during the burst. Stop new runs, then allow the
      // bounded acquisition to finish before reclaiming ADC1.
      const uint32_t startedUs = micros();
      while ((RTC_SLOW_MEM[COMPLETE_WORD] & UINT16_MAX) != sampleCount &&
             static_cast<uint32_t>(micros()) - startedUs < burstGuardUs)
        delayMicroseconds(10);
      delayMicroseconds(10);  // Completion is stored immediately before HALT.
      SET_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_RESET);
      // Also release the input if a rejected sleep/early wake interrupted the program.
      REG_SET_FIELD(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD, 0);
      SET_PERI_REG_MASK(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_FORCE);
      // An early wake may let the ULP power down SAR after IDF restored it.
      // Restore IDF's awake FSM state before another CPU conversion can start.
      REG_SET_FIELD(SENS_SAR_POWER_XPD_SAR_REG, SENS_FORCE_XPD_SAR, 0);
      SET_PERI_REG_MASK(SENS_SAR_PERI_CLK_GATE_CONF_REG, SENS_SARADC_CLK_EN);
    }
#endif
  }  // namespace

  bool readMilliVolts(uint8_t pin, uint16_t& milliVolts) {
    if (prepared) return false;
#ifdef LEAF_SIM
    milliVolts = analogReadMilliVolts(pin);
    return true;
#else
    adc_channel_t channel;
    esp_err_t error = channelForPin(pin, channel);
    if (error == ESP_OK) error = initializeCpuAdc();
    if (error == ESP_OK) error = initializeCalibration();
    if (error == ESP_OK && !(configuredChannels & (1U << channel))) {
      const adc_oneshot_chan_cfg_t config = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
      error = adc_oneshot_config_channel(cpuAdc, channel, &config);
      if (error == ESP_OK) configuredChannels |= 1U << channel;
    }
    int value = 0;
    if (error == ESP_OK)
      error = adc_oneshot_get_calibrated_result(cpuAdc, calibration, channel, &value);
    if (error != ESP_OK) {
      Serial.printf("ADC read GPIO %u failed: %s\n", pin, esp_err_to_name(error));
      return false;
    }
    milliVolts = value;
    return true;
#endif
  }

  esp_err_t prepare(uint8_t pin, uint32_t plannedSleepUs, Mode mode) {
    if (prepared) return ESP_ERR_INVALID_STATE;
    sampleError = ESP_OK;
    sampleCount = mode == Mode::Single ? 1 : SAMPLE_COUNT;
    settleCycles = mode == Mode::LongSettle ? 40000 : 2000;
    burstGuardUs = mode == Mode::LongSettle ? 15000 : 1000;
    if (plannedSleepUs < (mode == Mode::LongSettle ? 40000U : MIN_SLEEP_US)) {
      sampleError = ESP_ERR_NOT_SUPPORTED;
      return sampleError;
    }
    sampleDelayUs = plannedSleepUs / 2;
#ifdef LEAF_SIM
    samplePin = pin;
    sampleStartedUs = sim::clock().nowUs();
    prepared = true;
#else
    adc_channel_t channel;
    sampleError = channelForPin(pin, channel);
    if (sampleError == ESP_OK) sampleError = initializeCalibration();
    if (sampleError != ESP_OK) return sampleError;
    if (cpuAdc) {
      sampleError = adc_oneshot_del_unit(cpuAdc);
      if (sampleError != ESP_OK) return sampleError;
      cpuAdc = nullptr;
    }
    const ulp_adc_cfg_t config = {.adc_n = ADC_UNIT_1,
                                  .channel = channel,
                                  .atten = ADC_ATTEN_DB_12,
                                  .width = ADC_BITWIDTH_12,
                                  .ulp_mode = ADC_ULP_MODE_FSM};
    sampleError = ulp_adc_init(&config);
    if (sampleError != ESP_OK) return sampleError;
    ulpAdc = true;
    prepared = true;  // finish() also cleans up partial preparation.
    CLEAR_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_RESET);
    RTC_SLOW_MEM[RESULT_WORD] = 0;
    RTC_SLOW_MEM[COMPLETE_WORD] = 0;
    for (unsigned i = 0; i < BURST_CONVERSIONS; ++i) RTC_SLOW_MEM[CONVERSIONS_WORD + i] = 0;
    sampleError = loadSampler(channel);
    if (sampleError == ESP_OK) {
      sampleError = esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
      rtcPeriphHeld = sampleError == ESP_OK;
    }
    // ESP32-S3 retains RTC slow memory without a separate configurable power domain.
    if (sampleError == ESP_OK) sampleError = ulp_set_wakeup_period(0, sampleDelayUs);
    if (sampleError == ESP_OK) sampleError = ulp_run(0);
#endif
    return sampleError;
  }

  Result finish(bool acceptSamples, uint32_t elapsedUs) {
    Result result;
    result.error = sampleError;
    result.settleCycles = settleCycles;
    if (!prepared) return result;
#ifdef LEAF_SIM
    if (sampleError == ESP_OK && acceptSamples && elapsedUs > sampleDelayUs + burstGuardUs &&
        sim::clock().nowUs() - sampleStartedUs > sampleDelayUs + burstGuardUs) {
      result.milliVolts = analogReadMilliVolts(samplePin);
      result.firstMilliVolts = result.milliVolts;
      result.samples = sampleCount;
    }
#else
    stopSampler();
    // Exclude results from a rejected sleep or an early wake before the burst.
    if (result.error == ESP_OK && acceptSamples && elapsedUs > sampleDelayUs + 1000 &&
        (RTC_SLOW_MEM[COMPLETE_WORD] & UINT16_MAX) == sampleCount) {
      result.raw = (RTC_SLOW_MEM[RESULT_WORD] & UINT16_MAX) / sampleCount;
      for (unsigned i = 0; i < BURST_CONVERSIONS; ++i)
        result.conversions[i] = RTC_SLOW_MEM[CONVERSIONS_WORD + i] & UINT16_MAX;
      int value = 0;
      result.error = adc_cali_raw_to_voltage(calibration, result.raw, &value);
      if (result.error == ESP_OK) {
        result.milliVolts = value;
        result.error = adc_cali_raw_to_voltage(calibration, result.conversions[0], &value);
        if (result.error == ESP_OK) {
          result.firstMilliVolts = value;
          result.samples = sampleCount;
        }
      }
    }
    if (ulpAdc) {
      const esp_err_t error = ulp_adc_deinit();
      if (result.error == ESP_OK) result.error = error;
      ulpAdc = false;
    }
    if (rtcPeriphHeld) {
      // ON/OFF requests are reference-counted. Release ours before restoring AUTO.
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
      rtcPeriphHeld = false;
    }
#endif
    prepared = false;
    return result;
  }

  AwakeCheck checkWhileAwake(uint8_t pin) {
    AwakeCheck check;
    if (prepared) {
      check.error = ESP_ERR_INVALID_STATE;
      return check;
    }
    auto cpuAverage = [pin](uint16_t& milliVolts) {
      uint32_t sum = 0;
      for (unsigned i = 0; i < SAMPLE_COUNT; ++i) {
        uint16_t value = 0;
        if (!readMilliVolts(pin, value)) return false;
        sum += value;
      }
      milliVolts = (sum + SAMPLE_COUNT / 2) / SAMPLE_COUNT;
      return true;
    };
    if (!cpuAverage(check.cpuBeforeMv)) {
      check.error = ESP_FAIL;
      return check;
    }
    check.error = prepare(pin, MIN_SLEEP_US);
    const uint32_t startedUs = micros();
    if (check.error == ESP_OK) delayMicroseconds(7000);
    check.ulp = finish(check.error == ESP_OK, static_cast<uint32_t>(micros()) - startedUs);
    if (check.error == ESP_OK) check.error = check.ulp.error;
    if (check.error == ESP_OK && check.ulp.samples != 1) check.error = ESP_ERR_TIMEOUT;
    if (!cpuAverage(check.cpuAfterMv) && check.error == ESP_OK) check.error = ESP_FAIL;
    return check;
  }
}  // namespace sleep_adc
