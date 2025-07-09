#pragma once

#include <array>
#include <cstddef>

/// @brief Keeps track of a running average.
/// @tparam TValue Numeric value type; must support addition, subtraction, division, and casting
/// from size_t.
/// @tparam MaxSamples Maximum number of samples over which to average (though a smaller number may
/// be used at runtime).
template <typename TValue, size_t MaxSamples>
class RunningAverage {
 public:
  RunningAverage(size_t nInitial = MaxSamples)
      : sum(0), count(0), sampleCount(MaxSamples), index(0) {
    samples.fill(0);
    setSampleCount(nInitial);
  }

  void update(TValue value) {
    // Note: average will become NaN if value is NaN
    if (count < sampleCount) {
      sum += value;
      samples[count++] = value;
    } else {
      sum -= samples[index];
      sum += value;
      samples[index] = value;
    }
    index = (index + 1) % sampleCount;
  }

  void setSampleCount(size_t n) {
    if (n > MaxSamples) {
      n = MaxSamples;
    }
    if (n < 1) {
      n = 1;
    }
    sampleCount = n;
    recomputeSum();
  }

  TValue getAverage() const { return count > 0 ? sum / static_cast<TValue>(count) : 0; }

 private:
  void recomputeSum() {
    sum = 0;
    count = (count > sampleCount) ? sampleCount : count;
    for (size_t i = 0; i < count; ++i) {
      sum += samples[i];
    }
    index = count % sampleCount;
  }

  std::array<TValue, MaxSamples> samples;
  TValue sum;
  size_t count;
  size_t sampleCount;
  size_t index;
};
