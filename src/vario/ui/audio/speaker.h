#pragma once

#include <Arduino.h>

#include "hardware/configuration.h"
#include "ui/audio/dynamic_effects.h"
#include "ui/audio/notes.h"

using sound_t = const note::note_t*;

enum class SpeakerVolume : uint8_t { Off = 0, Low = 1, Medium = 2, High = 3 };

class Speaker {
 public:
  void init();
  void mute();
  void unMute();

  void setVolume(SpeakerVolume volume);

  // Update the sound the vario plays according to climb/sink rate in cm/s
  void updateVarioNote(int32_t verticalRate);

  // Call every 10ms to play notes.  Returns true if there are notes left to play.
  bool update();

  void playSound(sound_t sound);

  void playNote(note::note_t note);
  void debugPrint();

 private:
  // count timer cycles so we only update note every 4th time
  uint8_t speakerTimerCount_ = 0;

  SpeakerVolume speakerVolume_ = SpeakerVolume::Low;
  bool speakerMute_ = false;  // use to mute sound for various charging & sleep states

  // volatile pointer to the sound sample to play
  volatile sound_t soundPlaying_;

  // notes we should play, and if we're currently playing
  volatile note::note_t varioNote_ = note::NONE;      // note to play for vario beeps
  volatile note::note_t varioNoteLast_ = note::NONE;  // last note played for vario beeps
  volatile note::note_t fxNoteLast_ = note::NONE;     // last note played for sound effects
  volatile bool betweenVarioBeeps_ = false;           // are we resting (silence) between beeps?
  volatile bool playingSound_ = false;                // are we playing a sound?

  // == trackers for fixed-sample length speaker timer approach #1 ==

  // amount of samples we should play for
  volatile uint16_t varioPlaySamples_ = CLIMB_PLAY_SAMPLES_MAX;

  // amount of samples we should rest for
  volatile uint16_t varioRestSamples_ = CLIMB_REST_SAMPLES_MAX;

  // track how many samples (beats) we've played per note when playing sound effects (using
  // method #1 -- fixed sample length)
  volatile uint8_t varioPlaySampleCount_ = 0;

  // track how many samples (beats) we've played per note when playing sound effects (using
  // method #1 -- fixed sample length)
  volatile uint8_t varioRestSampleCount_ = 0;

  // track how many samples (beats) we've played per note when playing sound effects (using
  // method #1 -- fixed sample length)
  volatile uint8_t fxSampleCount_ = 0;

  // this is to allow playing single notes by changing single_note[0], while
  // still having a NOTE_END terminator following.
  uint16_t singleNote_[2] = {0, note::END};
};

extern Speaker speaker;
