#pragma once
#include "protocol.h"
#include "ui/audio/vario_audio.h"

namespace training {
  // No IO, local sensors, settings service or heap allocation. Only the selected climb drives
  // audio.
  class ClimbAudio {
   public:
    explicit ClimbAudio(const VarioAudioProfile& profile = {},
                        int32_t climbStart = vario_audio::DEFAULT_CLIMB_START_CMS,
                        int32_t sinkAlarm = vario_audio::DEFAULT_SINK_ALARM_CMS)
        : profile_(profile), climbStart_(climbStart), sinkAlarm_(sinkAlarm) {}
    void reset() {
      voice_.reset();
      active_ = false;
      output_ = 0;
    }
    uint16_t update(const Receiver& receiver, uint32_t now, bool enabled, bool effect = false) {
      if (!enabled || !receiver.climbFresh(now)) {
        reset();
        return 0;
      }
      const auto& student = receiver.students[receiver.focus];
      const bool changed =
          !active_ || focus_ != receiver.focus || id_ != student.id || session_ != student.session;
      if (changed) {
        reset();
        active_ = true;
        focus_ = receiver.focus;
        id_ = student.id;
        session_ = student.session;
        frameAt_ = now - vario_audio::FRAME_MS;
        pitchAt_ = now;
      }
      if (changed || sampleAt_ != student.receivedAt) {
        sampleAt_ = student.receivedAt;
        voice_.set(vario_audio::pattern(student.data.climbCms, profile_, climbStart_, sinkAlarm_));
      }
      if (effect) {
        voice_.resetSmoothing();
        vario_audio::advanceFrame(now, frameAt_);
        pitchAt_ = now;
        output_ = 0;
        return 0;  // Local button/test effects pause the envelope, like production SFX.
      }
      if (uint32_t(now - pitchAt_) >= vario_audio::PITCH_STEP_MS) {
        pitchAt_ = now;
        if (!voice_.active())
          voice_.resetSmoothing();
        else if (voice_.smoothed())
          output_ = voice_.smooth();
      }
      if (vario_audio::advanceFrame(now, frameAt_)) output_ = voice_.frame();
      if (!voice_.note()) output_ = 0;
      return output_;
    }

   private:
    VarioAudioProfile profile_;
    int32_t climbStart_, sinkAlarm_;
    vario_audio::Voice voice_;
    uint32_t frameAt_ = 0, pitchAt_ = 0, sampleAt_ = 0, id_ = 0, session_ = 0;
    uint8_t focus_ = 0;
    uint16_t output_ = 0;
    bool active_ = false;
  };
}  // namespace training
