#pragma once

#include <Arduino.h>

#include "hardware/configuration.h"
#include "ui/audio/dynamic_effects.h"
#include "ui/audio/notes.h"
#include "ui/audio/sound_effects.h"
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

  // Cancel any sound playing and do not play any sounds until unmuted
  void mute();

  // Resume normal sounds (previous volume levels, etc)
  void unMute();

  // Play sounds on the specified channel at the specified volume
  void setVolume(SoundChannel channel, SpeakerVolume volume);

  // Update the sound the vario plays according to climb/sink rate in cm/s
  void updateVarioNote(int32_t verticalRate);

  // Call periodically to play sounds.  Returns true if there are notes left to play.
  bool update();

  // Set the specified sound to be played
  void playSound(sound_t sound);

  // Set the specified note to be played once as a sound
  void playNote(note::note_t note);

 private:
  State state_;

  unsigned long tLastUpdate_;

  SpeakerVolume fxVolume_ = SpeakerVolume::Low;
  SpeakerVolume varioVolume_ = SpeakerVolume::Low;
  SpeakerVolume currentVolume_;
  bool speakerMute_ = false;  // use to mute sound for various charging & sleep states

  // pointer to the sound sample to play (moved to next note in sound as notes are played)
  sound_t soundPlaying_ = fx::silence;

  // notes we should play, and if we're currently playing
  note::note_t varioNote_ = note::NONE;      // note to play for vario beeps
  note::note_t varioNoteLast_ = note::NONE;  // last note played for vario beeps
  note::note_t fxNoteLast_ = note::NONE;     // last note played for sound effects
  bool betweenVarioBeeps_ = false;           // are we resting (silence) between beeps?
  bool playingSound_ = false;                // are we playing a sound?

  uint32_t lastTone_ = 0;

  // == trackers for fixed-sample length speaker timer approach #1 ==

  // amount of samples we should play for
  uint16_t varioPlaySamples_ = CLIMB_PLAY_SAMPLES_MAX;

  // amount of samples we should rest for
  uint16_t varioRestSamples_ = CLIMB_REST_SAMPLES_MAX;

  // track how many samples (beats) we've played per note when playing sound effects (using
  // method #1 -- fixed sample length)
  uint8_t varioPlaySampleCount_ = 0;

  // track how many samples (beats) we've played per note when playing sound effects (using
  // method #1 -- fixed sample length)
  uint8_t varioRestSampleCount_ = 0;

  // track how many samples (beats) we've played per note when playing sound effects (using
  // method #1 -- fixed sample length)
  uint8_t fxSampleCount_ = 0;

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

  void playTone(uint32_t freq);
};

extern Speaker speaker;
