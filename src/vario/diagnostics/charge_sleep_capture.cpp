#include "diagnostics/charge_sleep_capture.h"

#include <SD_MMC.h>
#include <WiFi.h>

#include <memory>
#include <new>

#include "diagnostics/diagnostic_logs.h"
#include "logbook/logbook_io.h"
#include "power.h"
#include "ui/settings/settings.h"

namespace charge_sleep_capture {
  namespace {
    constexpr uint32_t CAPTURE_US = 60'000'000;
    constexpr size_t MAX_SAMPLES = 120;  // One row per 500 ms charge update.
    constexpr char CSV_HEADER[] =
        "elapsed_us,eco_mode,button_block,diagnostic_block,leaf_log_block,usb_block,wifi_mode,"
        "input_limit,charging,battery_mv,iset_mv,charge_ma,sleep_attempt,requested_us,"
        "timer_setup_result,sleep_result,wake_cause,sleep_call_us,awake_charge_ma,"
        "ulp_raw,ulp_iset_mv,ulp_samples,ulp_error,ulp_attempt,ulp_battery,ulp_batt_raw,"
        "ulp_batt_sense_mv,ulp_batt_samples,awake_batt_sense_mv,ulp_settle_cycles,"
        "ulp_first_raw,ulp_sample_1_raw,ulp_sample_2_raw,ulp_sample_3_raw,ulp_sample_4_raw,"
        "ulp_sample_5_raw,ulp_sample_6_raw,ulp_sample_7_raw,ulp_sample_8_raw,ulp_first_mv\n";

    struct Sample {
      uint32_t elapsedUs;
      uint16_t batteryMv;
      uint16_t batterySenseMv;
      uint16_t isetMv;
      uint16_t chargeMa;
      uint16_t awakeChargeMa;
      uint8_t blockers;
      uint8_t wifiMode;
      PowerInputLevel inputLimit;
      bool eco;
      bool charging;
      bool attempted = false;
      Sleep sleep;
    };

    std::unique_ptr<Sample[]> samples;
    size_t count = 0;
    uint32_t startUs = 0;
    State captureState = State::Idle;

    bool writeSample(File& file, const Sample& sample) {
      const Sleep& sleep = sample.sleep;
      char row[320];
      const int length = snprintf(
          row, sizeof(row),
          "%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu,%d,%d,%d,%lu,%d,%d,%d,%d,%d,%d,"
          "%d,%d,%d,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
          static_cast<unsigned long>(sample.elapsedUs), sample.eco,
          !!(sample.blockers & ButtonHeld), !!(sample.blockers & DiagnosticNetwork),
          !!(sample.blockers & LeafLog), !!(sample.blockers & UsbHost), sample.wifiMode,
          static_cast<int>(sample.inputLimit), sample.charging, sample.batteryMv, sample.isetMv,
          sample.chargeMa, sample.attempted, static_cast<unsigned long>(sleep.requestedUs),
          sleep.timerResult, sleep.result, sleep.wakeCause,
          static_cast<unsigned long>(sleep.callUs), sample.awakeChargeMa,
          sleep.ulpBattery ? 0 : sleep.ulp.raw, sleep.ulpBattery ? 0 : sleep.ulp.milliVolts,
          sleep.ulpBattery ? 0 : sleep.ulp.samples, sleep.ulp.error, sleep.ulpAttempted,
          sleep.ulpBattery, sleep.ulpBattery ? sleep.ulp.raw : 0,
          sleep.ulpBattery ? sleep.ulp.milliVolts : 0, sleep.ulpBattery ? sleep.ulp.samples : 0,
          sample.batterySenseMv, sleep.ulp.settleCycles, sleep.ulp.conversions[0],
          sleep.ulp.conversions[1], sleep.ulp.conversions[2], sleep.ulp.conversions[3],
          sleep.ulp.conversions[4], sleep.ulp.conversions[5], sleep.ulp.conversions[6],
          sleep.ulp.conversions[7], sleep.ulp.conversions[8], sleep.ulp.firstMilliVolts);
      return length > 0 && static_cast<size_t>(length) < sizeof(row) &&
             file.write(reinterpret_cast<const uint8_t*>(row), length) ==
                 static_cast<size_t>(length);
    }
  }  // namespace

  State state() { return captureState; }

  bool arm() {
    if (captureState != State::Idle) return false;
    samples.reset(new (std::nothrow) Sample[MAX_SAMPLES]);
    if (!samples) return false;
    captureState = State::Armed;
    return true;
  }

  void finish() {
    if (captureState == State::Armed) {
      samples.reset();
      captureState = State::Idle;
    } else if (captureState == State::Recording) {
      captureState = State::Ready;
    }
  }

  void recordTick(uint32_t tickUs, uint8_t blockers) {
    if (captureState == State::Armed) {
      startUs = tickUs;
      captureState = State::Recording;
    }
    if (captureState != State::Recording) return;

    const uint32_t elapsedUs = tickUs - startUs;
    if (count == MAX_SAMPLES || elapsedUs >= CAPTURE_US) {
      finish();
      return;
    }

    const auto& info = power.info();
    samples[count++] = {elapsedUs,
                        info.batteryMV,
                        info.batterySenseMV,
                        info.chargeSenseMV,
                        info.chargeCurrentMA,
                        info.awakeChargeCurrentMA,
                        blockers,
                        static_cast<uint8_t>(WiFi.getMode()),
                        info.inputCurrent,
                        settings.system_ecoMode,
                        info.charging,
                        false,
                        {}};
  }

  void recordButtonBlock() {
    if (captureState == State::Recording) samples[count - 1].blockers |= ButtonHeld;
  }

  void recordSleep(const Sleep& sleep) {
    if (captureState != State::Recording) return;
    samples[count - 1].attempted = true;
    samples[count - 1].sleep = sleep;
  }

  bool exportToSd() {
    if (captureState != State::Ready) return false;
    logbook_io::Guard transaction;
    if (!transaction || !diagnostic_logs::ensureDirectory()) return false;

    char path[64];
    unsigned sequence = 0;
    do {
      snprintf(path, sizeof(path), "/diagnostics/charge_sleep_%u.csv", ++sequence);
    } while (SD_MMC.exists(path));
    File file = SD_MMC.open(path, "w", true);
    if (!file) return false;

    bool written = file.write(reinterpret_cast<const uint8_t*>(CSV_HEADER),
                              sizeof(CSV_HEADER) - 1) == sizeof(CSV_HEADER) - 1;
    for (size_t i = 0; written && i < count; ++i) written = writeSample(file, samples[i]);
    file.flush();
    file.close();
    if (!written) {
      SD_MMC.remove(path);
      return false;
    }

    Serial.printf("Charge sleep capture: %s\n", path);
    samples.reset();
    count = 0;
    captureState = State::Idle;
    return true;
  }
}  // namespace charge_sleep_capture
