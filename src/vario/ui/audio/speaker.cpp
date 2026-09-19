#include "ui/audio/speaker.h"

#include <Arduino.h>

#include "hardware/Leaf_SPI.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "logging/log.h"
#include "ui/audio/dynamic_effects.h"
#include "ui/audio/notes.h"
#include "ui/audio/sound_effects.h"
#include "ui/settings/settings.h"
#include "utils/magic_enum.h"

Speaker speaker;

namespace {
  constexpr unsigned long NOTE_DURATION_MS = 40;
  constexpr unsigned long TIMING_TOLERANCE_MS = 2;
}  // namespace

void Speaker::init(void) {
  assertState("Speaker::init", State::Uninitialized);

  // configure speaker pinout and PWM channel
  pinMode(SPEAKER_PIN, OUTPUT);
  ledcAttach(SPEAKER_PIN, 1000, 10);

  // set speaker volume pins as outputs IF NOT ON the IO Expander
  if (!SPEAKER_VOLA_IOEX) pinMode(SPEAKER_VOLA, OUTPUT);
  if (!SPEAKER_VOLB_IOEX) pinMode(SPEAKER_VOLB, OUTPUT);

  tLastUpdate_ = NOTE_DURATION_MS * (millis() / NOTE_DURATION_MS);
  state_ = State::Active;
  setVolume(varioVolume_, true);
}

void Speaker::mute() {
  assertState("Speaker::mute", State::Uninitialized, State::Active);
  playingSound_ = false;  // clear FX sound
  updateVarioNote(0);     // clear vario note
  if (state_ != State::Uninitialized) {
    playTone(0);  // mute speaker pin
  }
  speakerMute_ = true;
}

void Speaker::unMute() {
  assertState("Speaker::unMute", State::Uninitialized, State::Active);
  speakerMute_ = false;
}

void Speaker::setVolume(SoundChannel channel, SpeakerVolume volume) {
  if (channel == SoundChannel::FX) {
    fxVolume_ = volume;
  } else if (channel == SoundChannel::Vario) {
    varioVolume_ = volume;
  } else {
    fatalError("Channel %s (%u) invalid in Speaker::setVolume", nameOf(channel).c_str(), channel);
  }
}

void Speaker::setVolume(SpeakerVolume volume, bool force) {
  assertState("Speaker::setVolume", State::Active);

  if (currentVolume_ == volume && !force) {
    // Already at specified volume; no need to change
    return;
  }

  // This routine isn't applicable for certain hardware variants
  if (SPEAKER_VOLA == NC || SPEAKER_VOLB == NC) return;

  switch (volume) {
    case SpeakerVolume::Off:  // No Volume -- disable piezo speaker driver
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 0);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 0);
      break;
    case SpeakerVolume::Low:  // Low Volume -- enable piezo speaker driver 1x (3.3V)
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 1);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 0);
      break;
    case SpeakerVolume::Medium:  // Med Volume -- enable piezo speaker driver 2x (6.6V)
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 0);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 1);
      break;
    case SpeakerVolume::High:  // High Volume -- enable piezo spaker driver 3x (9.9V)
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 1);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 1);
      break;
    default:
      fatalError("Speaker set to invalid volume %d", (uint8_t)volume);
  }
  currentVolume_ = volume;
}

void Speaker::playSound(sound_t sound) {
  assertState("Speaker::playSound", State::Uninitialized, State::Active);
  Serial.printf("%d playSound %d\n", millis(), sound);
  soundPlaying_ = sound;
  playingSound_ = true;
}

void Speaker::playNote(uint16_t note) {
  assertState("Speaker::playNote", State::Uninitialized, State::Active);
  singleNote_[0] = note;
  playSound(singleNote_);
}

void Speaker::updateVarioNote(int32_t verticalRate) {
  assertState("Speaker::updateVarioNote", State::Uninitialized, State::Active);

  if (varioTestActive_) return;
  setVarioNote(verticalRate, true);
}

void Speaker::startVarioTest(int32_t targetVerticalRate) {
  assertState("Speaker::startVarioTest", State::Uninitialized, State::Active);
  varioTestTargetRate_ = targetVerticalRate;
  varioTestStartedMs_ = millis();
  varioTestActive_ = targetVerticalRate != 0;
  setVarioNote(0, false);
}

void Speaker::setVarioNote(int32_t verticalRate, bool respectQuietMode) {
  assertState("Speaker::setVarioNote", State::Uninitialized, State::Active);

  // don't play any beeps if Quiet Mode is turned on, and we haven't started a flight
  if (respectQuietMode && settings.vario_quietMode && !flightTimer_isRunning()) {
    varioNote_ = note::NONE;
    return;
  }

  uint16_t newVarioNote = note::NONE;
  uint16_t newVarioPlaySamples = 0;
  uint16_t newVarioRestSamples = 0;

  const bool sinkAlarmEnabled = settings.vario_sinkAlarm < 0.0f;
  int sinkAlarm_cms;
  if (settings.vario_sinkAlarm_units) {
    sinkAlarm_cms = settings.vario_sinkAlarm * 100 / 196.85;  // convert fpm to cm/s
  } else {
    sinkAlarm_cms = settings.vario_sinkAlarm * 100;  // convert m/s to cm/s
  }

  if (verticalRate >= settings.vario_climbStart) {
    const int32_t clampedRate =
        verticalRate > settings.varioAudio.climbMax ? settings.varioAudio.climbMax : verticalRate;
    const int32_t ratePastStart = clampedRate - settings.vario_climbStart;
    const int32_t rateRange = settings.varioAudio.climbMax - settings.vario_climbStart;
    newVarioNote = settings.varioAudio.climbNoteStart +
                   ratePastStart *
                       (settings.varioAudio.climbNoteMax - settings.varioAudio.climbNoteStart) /
                       rateRange;

    if (verticalRate >= settings.varioAudio.climbContinuous) {
      newVarioPlaySamples = 1;
      newVarioRestSamples = 0;  // just hold a continuous tone, no rest in between
    } else {
      const int32_t timingRange = settings.varioAudio.climbContinuous - settings.vario_climbStart;
      const int32_t timingProgress = verticalRate - settings.vario_climbStart;
      newVarioPlaySamples =
          settings.varioAudio.climbPlaySamplesMax -
          (timingProgress * settings.varioAudio.climbPlaySamplesMax / timingRange);
      newVarioRestSamples =
          settings.varioAudio.climbRestSamplesMax -
          (timingProgress * settings.varioAudio.climbRestSamplesMax / timingRange);
    }

    // if we trigger sink threshold
  } else if (sinkAlarmEnabled && verticalRate <= sinkAlarm_cms) {
    const int32_t sinkRateRange = sinkAlarm_cms - settings.varioAudio.sinkMax;
    const int32_t clampedRate =
        verticalRate < settings.varioAudio.sinkMax ? settings.varioAudio.sinkMax : verticalRate;
    const int32_t sinkRatePastAlarm = sinkAlarm_cms - clampedRate;
    newVarioNote = settings.varioAudio.sinkNoteStart -
                   sinkRatePastAlarm *
                       (settings.varioAudio.sinkNoteStart - settings.varioAudio.sinkNoteMin) /
                       sinkRateRange;

    if (verticalRate <= settings.varioAudio.sinkContinuous) {
      newVarioPlaySamples = 1;
      newVarioRestSamples = 0;  // just hold a continuous tone, no pulses
    } else {
      const int32_t timingRange = sinkAlarm_cms - settings.varioAudio.sinkContinuous;
      const int32_t timingProgress = sinkAlarm_cms - verticalRate;
      newVarioRestSamples = settings.varioAudio.sinkRestSamplesMin -
                            (timingProgress * settings.varioAudio.sinkRestSamplesMin / timingRange);
      newVarioPlaySamples = settings.varioAudio.sinkPlaySamplesMin +
                            (settings.varioAudio.sinkRestSamplesMin - newVarioRestSamples);
    }

  } else {
    // Stay fully silent in the deadband. These defaults also prevent stale stack values from being
    // interpreted as a very low PWM frequency by the simulator (or an arbitrary tone on-device).
    betweenVarioBeeps_ = false;
    varioPlaySampleCount_ = 0;
    varioRestSampleCount_ = 0;
  }

  varioNote_ = newVarioNote;
  varioPlaySamples_ = newVarioPlaySamples;
  varioRestSamples_ = newVarioRestSamples;
}

void Speaker::updateVarioTest() {
  if (!varioTestActive_) return;

  const uint32_t elapsed = millis() - varioTestStartedMs_;
  const uint32_t rampDownStart = varioTestRampMs() + varioTestHoldMs();
  const uint32_t end = rampDownStart + varioTestRampMs();
  int32_t verticalRate = 0;
  if (elapsed < varioTestRampMs()) {
    verticalRate = static_cast<int32_t>(static_cast<int64_t>(varioTestTargetRate_) * elapsed /
                                        varioTestRampMs());
  } else if (elapsed < rampDownStart) {
    verticalRate = varioTestTargetRate_;
  } else if (elapsed < end) {
    verticalRate = static_cast<int32_t>(static_cast<int64_t>(varioTestTargetRate_) *
                                        (end - elapsed) / varioTestRampMs());
  } else {
    varioTestActive_ = false;
  }
  setVarioNote(verticalRate, false);
}

bool Speaker::update() {
  if (state_ == State::Uninitialized) {
    init();
  }
  if (state_ != State::Active) {
    fatalError("Unsupported Speaker::update state %d (%u)", nameOf(state_).c_str(), state_);
  }

  // If speaker is muted, ensure silence and don't play sound
  if (speakerMute_) {
    playTone(0);
    return false;
  }

  if (!shouldUpdate()) {
    return playingSound_;
  }

  updateVarioTest();

  if (playingSound_ && fxVolume_ != SpeakerVolume::Off) {
    // prioritize sound effects from UI & Button etc before we get to vario beeps
    // but only play soundFX if system volume is on
    return updateSound();

  } else if (varioNote_ != note::NONE && varioVolume_ != SpeakerVolume::Off) {
    // if there's a vario note to play, and the vario volume isn't zero
    updateVario();
    return false;

  } else {
    // play silence
    playTone(0);
  }

  return false;
}

bool Speaker::shouldUpdate() {
  unsigned long tNow = millis();
  // Nominally wait NOTE_DURATION_MS intervals between updates, but allow an update to happen up to
  // TIMING_TOLERANCE_MS before its actual target time.  In diagrams below, NOTE_DURATION_MS
  // intervals are |, tLastUpdate_ is x, tNow is y, and tLastUpdate_ should be updated to z.
  // TIMING_TOLERANCE_MS is one character wide.
  // x-------y-|---------|---------|---------| (no action)
  // x--------y|---------z---------|---------|
  // x---------y---------z---------|---------|
  // x---------|y--------z---------|---------|
  // x---------|-y-------z---------|---------|
  // x---------|-------y-z---------|---------|
  // x---------|--------y|---------z---------|
  // x---------|---------y---------z---------|
  // x---------|---------|y--------z---------|
  // x---------|---------|-y-------z---------|

  // n is the number of NOTE_DURATION_MS intervals that have elapsed, or almost elapsed, since
  // tLastUpdate_
  uint32_t n = (tNow + TIMING_TOLERANCE_MS - tLastUpdate_) / NOTE_DURATION_MS;
  if (n == 0) {
    // Nothing to do yet; we need to wait longer.
    return false;
  }
  if (n >= 2) {
    Serial.printf("Speaker::update skipped %d intervals\n", n - 1);
  }
  tLastUpdate_ += n * NOTE_DURATION_MS;
  return true;
}

bool Speaker::updateSound() {
  setVolume(fxVolume_);
  if (*soundPlaying_ != note::END) {
    playTone(*soundPlaying_);
    fxNoteLast_ = *soundPlaying_;  // save last note

    // if we've played this note for enough samples
    if (++fxSampleCount_ >= FX_NOTE_SAMPLE_COUNT) {
      soundPlaying_++;
      fxSampleCount_ = 0;  // and reset sample count
    }
    return true;

  } else {  // Else, we're at END_OF_TONE
    playTone(0);
    playingSound_ = false;
    fxNoteLast_ = note::NONE;
    return false;
  }
}

void Speaker::updateVario() {
  setVolume(varioVolume_);
  //  Handle the beeps and rests of a vario sound "measure"
  if (betweenVarioBeeps_) {
    playTone(0);  // "play" silence since we're resting between beeps

    // stop playing rest if we've done it long enough
    if (++varioRestSampleCount_ >= varioRestSamples_) {
      varioRestSampleCount_ = 0;
      varioNoteLast_ = note::NONE;
      betweenVarioBeeps_ = false;  // next time through we want to play sound
    }

  } else {
    playTone(varioNote_);
    varioNoteLast_ = varioNote_;

    if (++varioPlaySampleCount_ >= varioPlaySamples_) {
      varioPlaySampleCount_ = 0;
      if (varioRestSamples_) betweenVarioBeeps_ = true;  // next time through we want to rest
    }
  }
}

void Speaker::playTone(uint32_t freq) {
  if (freq != lastTone_) {
    ledcWriteTone(SPEAKER_PIN, freq);
    lastTone_ = freq;
  }
}

void Speaker::onUnexpectedState(const char* action, State actual) const {
  fatalError("%s while %s", action, nameOf(actual));
}
