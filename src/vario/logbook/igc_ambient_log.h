#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>

// IGC Appendix A3.4/A4.4/A7: OAT (Celsius) and HUM (percent) in timestamped K records.
class IgcAmbientLog {
 public:
  static constexpr const char* DECLARATION = "J020813OAT1418HUM";
  static constexpr uint32_t INTERVAL_MS = 20000;

  void reset() { *this = {}; }

  template <typename Emit>
  void update(uint32_t nowMs, const char* utc, bool ambientValid, float temperatureC,
              float relativeHumidity, Emit emit) {
    if (written_ && uint32_t(nowMs - lastWriteMs_) < INTERVAL_MS) return;
    // No invented missing-value codes for standard fields: omit unavailable ambient pairs.
    if (!ambientValid || !isfinite(temperatureC) || temperatureC < -204.7f ||
        temperatureC > 204.7f || !isfinite(relativeHumidity) || relativeHumidity < 0 ||
        relativeHumidity > 100)
      return;
    const int32_t temperature = lroundf(temperatureC * 10);
    const uint32_t magnitude = temperature < 0 ? -temperature : temperature;
    const uint32_t humidity = lroundf(relativeHumidity * 10);
    char line[32];
    snprintf(line, sizeof(line), "K%s%s%0*lu.%lu%03lu.%lu", utc, temperature < 0 ? "-" : "",
             temperature < 0 ? 3 : 4, (unsigned long)(magnitude / 10),
             (unsigned long)(magnitude % 10), (unsigned long)(humidity / 10),
             (unsigned long)(humidity % 10));
    emit(line);
    written_ = true;
    lastWriteMs_ = nowMs;
  }

 private:
  bool written_ = false;
  uint32_t lastWriteMs_ = 0;
};
