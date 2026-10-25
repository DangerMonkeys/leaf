#pragma once
#include <cstdint>
#include "ui/audio/dynamic_effects.h"

// Shared by production settings and the sensor-independent instructor audio engine.
struct VarioAudioProfile {
  int32_t climbContinuous = CLIMB_CONTINUOUS;
  int32_t climbMax = CLIMB_MAX;
  uint16_t climbNoteStart = CLIMB_NOTE_START;
  uint16_t climbNoteMax = CLIMB_NOTE_MAX;
  uint16_t climbPlaySamplesMax = CLIMB_PLAY_SAMPLES_MAX;
  uint16_t climbRestSamplesMax = CLIMB_REST_SAMPLES_MAX;
  int32_t sinkContinuous = SINK_CONTINUOUS;
  int32_t sinkMax = SINK_MAX;
  uint16_t sinkNoteStart = SINK_NOTE_START;
  uint16_t sinkNoteMin = SINK_NOTE_MIN;
  uint16_t sinkPlaySamplesMin = SINK_PLAY_SAMPLES_MIN;
  uint16_t sinkRestSamplesMin = SINK_REST_SAMPLES_MIN;
};

namespace vario_audio {
  constexpr uint32_t FRAME_MS = 40, TOLERANCE_MS = 2, PITCH_STEP_MS = 10;
  constexpr int32_t DEFAULT_CLIMB_START_CMS = 5, DEFAULT_SINK_ALARM_CMS = -250;
  struct Pattern {
    uint16_t frequency = 0, play = 0, rest = 0;
  };

  // This mapping is the production Leaf climb/sink pitch and cadence calculation.
  inline Pattern pattern(int32_t rate, const VarioAudioProfile& p, int32_t climbStart,
                         int32_t sinkAlarm) {
    Pattern out;
    if (rate >= climbStart) {
      if (p.climbMax <= climbStart || p.climbContinuous <= climbStart) return out;
      const int32_t capped = rate > p.climbMax ? p.climbMax : rate;
      out.frequency = p.climbNoteStart + (capped - climbStart) *
                                             (int32_t(p.climbNoteMax) - p.climbNoteStart) /
                                             (p.climbMax - climbStart);
      if (rate >= p.climbContinuous) {
        out.play = 1;
      } else {
        const int32_t progress = rate - climbStart, range = p.climbContinuous - climbStart;
        out.play = p.climbPlaySamplesMax - progress * p.climbPlaySamplesMax / range;
        out.rest = p.climbRestSamplesMax - progress * p.climbRestSamplesMax / range;
      }
    } else if (sinkAlarm < 0 && rate <= sinkAlarm) {
      if (p.sinkMax >= sinkAlarm || p.sinkContinuous >= sinkAlarm) return out;
      const int32_t capped = rate < p.sinkMax ? p.sinkMax : rate;
      out.frequency = p.sinkNoteStart - (sinkAlarm - capped) *
                                            (int32_t(p.sinkNoteStart) - p.sinkNoteMin) /
                                            (sinkAlarm - p.sinkMax);
      if (rate <= p.sinkContinuous) {
        out.play = 1;
      } else {
        const int32_t progress = sinkAlarm - rate, range = sinkAlarm - p.sinkContinuous;
        out.rest = p.sinkRestSamplesMin - progress * p.sinkRestSamplesMin / range;
        out.play = p.sinkPlaySamplesMin + (p.sinkRestSamplesMin - out.rest);
      }
    }
    return out;
  }

  // A new climb sample changes the pattern, not the phase of the currently playing measure.
  // The caller advances frames at 40 ms and pitch smoothing at its 10 ms audio service cadence.
  class Voice {
   public:
    void set(Pattern next) {
      if (!next.frequency)
        reset();
      else
        pattern_ = next;
    }
    void reset() {
      pattern_ = {};
      playCount_ = restCount_ = smooth_ = 0;
      resting_ = false;
    }
    void quiet() { pattern_.frequency = 0; }
    void resetSmoothing() { smooth_ = 0; }
    uint16_t note() const { return pattern_.frequency; }
    uint16_t smoothed() const { return smooth_; }
    bool resting() const { return resting_; }
    bool active() const { return pattern_.frequency && !resting_; }
    uint16_t smooth() {
      if (!active() || !smooth_) return 0;
      uint32_t step = uint32_t(smooth_) * 40000U / 1000000U;
      if (!step) step = 1;
      if (pattern_.frequency > smooth_) {
        const uint32_t delta = pattern_.frequency - smooth_;
        smooth_ += step > delta ? delta : step;
      } else {
        const uint32_t delta = smooth_ - pattern_.frequency;
        smooth_ -= step > delta ? delta : step;
      }
      return smooth_;
    }
    uint16_t frame() {
      if (!pattern_.frequency) return 0;
      if (resting_) {
        if (++restCount_ >= pattern_.rest) {
          restCount_ = 0;
          resting_ = false;
        }
        return 0;
      }
      if (!smooth_) smooth_ = pattern_.frequency;
      const uint16_t output = smooth_;
      if (++playCount_ >= pattern_.play) {
        playCount_ = 0;
        if (pattern_.rest) resting_ = true;
      }
      return output;
    }

   private:
    Pattern pattern_;
    uint16_t playCount_ = 0, restCount_ = 0, smooth_ = 0;
    bool resting_ = false;
  };

  // Preserve production's tolerance and skipped-frame policy, including millis() rollover.
  inline uint32_t advanceFrame(uint32_t now, uint32_t& last) {
    const uint32_t count = uint32_t(now + TOLERANCE_MS - last) / FRAME_MS;
    if (count) last += count * FRAME_MS;
    return count;
  }
}  // namespace vario_audio
