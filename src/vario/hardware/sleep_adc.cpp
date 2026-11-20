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
    constexpr uint32_t MIN_SLEEP_US = 4000;
    uint32_t sampleDelayUs = 0;
    constexpr uint32_t ACQUISITION_GUARD_US = 1000;
    esp_err_t sampleError = ESP_OK;
    bool prepared = false;

#ifdef LEAF_SIM
    uint8_t samplePin = 0;
    uint64_t sampleStartedUs = 0;
#else
    // The framework reserves 512 bytes of RTC slow memory for the ULP.
    constexpr uint32_t RESULT_WORD = 126;
    constexpr uint32_t COMPLETE_WORD = 127;
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
      // One conversion only. Bench captures showed later conversions in a burst
      // systematically underread ISET; smooth calibrated readings across sleep cycles instead.
      const ulp_insn_t program[] = {
          I_END(),
          // IDF 5.3.2 forces SAR off at light-sleep entry, even in ULP mode.
          // Restore its clock/power for this acquisition only.
          I_WR_REG_BIT(SENS_SAR_PERI_CLK_GATE_CONF_REG, SENS_SARADC_CLK_EN_S, 1),
          I_WR_REG(SENS_SAR_POWER_XPD_SAR_REG, SENS_FORCE_XPD_SAR_S, SENS_FORCE_XPD_SAR_S + 1, 3),
          I_WR_REG(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_S, SENS_SAR1_EN_PAD_S + 9, 0),
          I_WR_REG_BIT(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_S + pad, 1),
          I_WR_REG_BIT(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_FORCE_S, 1), I_DELAY(2000),
          I_ADC(R0, 0, pad),
          // Power down immediately, before storing the result.
          I_WR_REG(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_S, SENS_SAR1_EN_PAD_S + 9, 0),
          I_WR_REG_BIT(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD_FORCE_S, 1),
          I_WR_REG(SENS_SAR_POWER_XPD_SAR_REG, SENS_FORCE_XPD_SAR_S, SENS_FORCE_XPD_SAR_S + 1, 2),
          I_WR_REG_BIT(SENS_SAR_PERI_CLK_GATE_CONF_REG, SENS_SARADC_CLK_EN_S, 0),
          I_MOVI(R1, RESULT_WORD), I_ST(R0, R1, 0), I_MOVI(R0, 1), I_ST(R0, R1, 1), I_HALT()};
      static_assert(sizeof(program) / sizeof(program[0]) <= RESULT_WORD,
                    "ULP program overlaps its result");
      size_t size = sizeof(program) / sizeof(program[0]);
      return ulp_process_macros_and_load(0, program, &size);
    }

    void stopSampler() {
      ulp_timer_stop();
      // A button may wake the CPU during acquisition. Stop new runs, then allow the
      // bounded acquisition to finish before reclaiming ADC1.
      const uint32_t startedUs = micros();
      while ((RTC_SLOW_MEM[COMPLETE_WORD] & UINT16_MAX) != 1 &&
             static_cast<uint32_t>(micros()) - startedUs < ACQUISITION_GUARD_US)
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

  esp_err_t prepare(uint8_t pin, uint32_t plannedSleepUs) {
    if (prepared) return ESP_ERR_INVALID_STATE;
    sampleError = ESP_OK;
    if (plannedSleepUs < MIN_SLEEP_US) {
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
    if (!prepared) return result;
#ifdef LEAF_SIM
    if (sampleError == ESP_OK && acceptSamples &&
        elapsedUs > sampleDelayUs + ACQUISITION_GUARD_US &&
        sim::clock().nowUs() - sampleStartedUs > sampleDelayUs + ACQUISITION_GUARD_US) {
      result.milliVolts = analogReadMilliVolts(samplePin);
      result.samples = 1;
    }
#else
    stopSampler();
    // Exclude results from a rejected sleep or an early wake before acquisition.
    if (result.error == ESP_OK && acceptSamples &&
        elapsedUs > sampleDelayUs + ACQUISITION_GUARD_US &&
        (RTC_SLOW_MEM[COMPLETE_WORD] & UINT16_MAX) == 1) {
      result.raw = RTC_SLOW_MEM[RESULT_WORD] & UINT16_MAX;
      int value = 0;
      result.error = adc_cali_raw_to_voltage(calibration, result.raw, &value);
      if (result.error == ESP_OK) {
        result.milliVolts = value;
        result.samples = 1;
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

}  // namespace sleep_adc
