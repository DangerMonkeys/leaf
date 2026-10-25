#pragma once

#include <Arduino.h>

#include "hardware/configuration.h"
#include "ui/audio/notes.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/vario_audio.h"
#include "utils/state_assert_mixin.h"

using sound_t = const note::note_t*;

enum class SpeakerVolume : uint8_t { Off = 0, Low = 1, Medium = 2, High = 3 };

// Speaker preferentially plays a single FX sound (note sequence) by moving the sound pointer
// forward until encountering the end-of-sound sentinel note value.  playSound and playNote both
// immediately replace this single sound with the specified sound.  When no sound FX is playing,
// Speaker plays vario notes corresponding to the climb rate provided via updateVarioNote.  The
// hardware speaker is actuated by calling update at least once per NOTE_DURATION_MS (see
// implementation).
class Speaker : private StateAssertMixin<Speaker> {
 public:
  enum class State : uint8_t { Uninitialized, Active };

  enum class SoundChannel : uint8_t { FX, Vario };

  State state() const { return state_; }

  // Cancel any sound playing and do not play any sounds until unmuted (though do play any sound
  // "played" while muted as soon as Speaker is unMuted)
  void mute();

  // Resume normal sounds (previous volume levels, etc)
  void unMute();

  // Play sounds on the specified channel at the specified volume
  void setVolume(SoundChannel channel, SpeakerVolume volume);
  SpeakerVolume volume(SoundChannel channel) const {
    return channel == SoundChannel::Vario ? varioVolume_ : fxVolume_;
  }

  // Update the sound the vario plays according to climb/sink rate in cm/s
  void updateVarioNote(int32_t verticalRate);

  // Temporarily replace live barometer input with a smooth 0 -> target -> 0 test sweep.
  void startVarioTest(int32_t targetVerticalRate);
  static constexpr uint32_t varioTestRampMs() { return 15000; }
  static constexpr uint32_t varioTestHoldMs() { return 1500; }
  static constexpr uint32_t varioTestDurationMs() {
    return varioTestRampMs() * 2 + varioTestHoldMs();
  }

  // Call periodically to play sounds.  Returns true if there are notes left to play.
  bool update();

  // Set the specified sound to be played
  void playSound(sound_t sound);

  // Set the specified note to be played once as a sound
  void playNote(note::note_t note);

  // Play one vario-frequency tone for a fixed duration, ignoring quiet mode.
  void playVarioToneFor(note::note_t note, uint32_t durationMs);

 private:
  State state_;

  uint32_t tLastUpdate_;

  SpeakerVolume fxVolume_ = SpeakerVolume::Low;
  SpeakerVolume varioVolume_ = SpeakerVolume::Low;
  SpeakerVolume currentVolume_;
  bool speakerMute_ = false;  // use to mute sound for various charging & sleep states

  // pointer to the sound sample to play (moved to next note in sound as notes are played)
  sound_t soundPlaying_ = fx::silence;

  // notes we should play, and if we're currently playing
  vario_audio::Voice vario_;
  note::note_t fxNoteLast_ = note::NONE;
  bool playingSound_ = false;  // are we playing a sound?

  // Sound effects retain their own sample count and production priority over vario audio.
  uint8_t fxSampleCount_ = 0;

  bool varioTestActive_ = false;
  int32_t varioTestTargetRate_ = 0;
  uint32_t varioTestStartedMs_ = 0;
  note::note_t previewTone_ = note::NONE;
  uint32_t previewToneUntilMs_ = 0;

  // this is to allow playing single notes by changing single_note[0], while
  // still having a NOTE_END terminator following.
  uint16_t singleNote_[2] = {0, note::END};

  void init();

  void setVolume(SpeakerVolume volume, bool force = false);

  void onUnexpectedState(const char* action, State actual) const;
  friend struct StateAssertMixin<Speaker>;

  bool shouldUpdate();
  bool updateSound();
  void updateVario();
  void updateVarioSmoothing();
  bool varioToneActive() const;
  void setVarioNote(int32_t verticalRate, bool respectQuietMode);
  void updateVarioTest();
};

extern Speaker speaker;
