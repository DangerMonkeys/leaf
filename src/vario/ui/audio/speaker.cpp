#include "ui/audio/speaker.h"

#include <Arduino.h>

#include "hardware/Leaf_SPI.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "logging/log.h"
#include "ui/audio/dynamic_effects.h"
#include "ui/audio/notes.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker_driver.h"
#include "ui/settings/settings.h"
#include "utils/magic_enum.h"

Speaker speaker;

namespace {
  constexpr uint32_t NOTE_DURATION_MS = vario_audio::FRAME_MS;
}  // namespace

void Speaker::init(void) {
  assertState("Speaker::init", State::Uninitialized);

  // configure speaker pinout for timer-driven square wave output
  speaker_driver::init();

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
  previewTone_ = note::NONE;
  updateVarioNote(0);  // clear vario note
  if (state_ != State::Uninitialized) {
    speaker_driver::playTone(0);  // mute speaker pin
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

void Speaker::playVarioToneFor(uint16_t note, uint32_t durationMs) {
  assertState("Speaker::playVarioToneFor", State::Uninitialized, State::Active);
  previewTone_ = note;
  previewToneUntilMs_ = millis() + durationMs;
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

  // Quiet mode remains a production-only gate; the instructor has no local flight timer.
  if (respectQuietMode && settings.vario_quietMode && !flightTimer_isRunning()) {
    vario_.quiet();
    return;
  }
  const int32_t sinkAlarm = settings.vario_sinkAlarm_units ? settings.vario_sinkAlarm * 100 / 196.85
                                                           : settings.vario_sinkAlarm * 100;
  vario_.set(vario_audio::pattern(verticalRate, settings.varioAudio, settings.vario_climbStart,
                                  sinkAlarm));
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

  if (!varioToneActive()) {
    // Leaving vario-tone context: forget smoothing state so next tone starts immediately.
    vario_.resetSmoothing();
  }

  // If speaker is muted, ensure silence and don't play sound
  if (speakerMute_) {
    speaker_driver::playTone(0);
    return false;
  }

  // Run vario smoothing independently of NOTE_DURATION_MS cadence so SFX timing is unaffected.
  updateVarioSmoothing();

  if (!shouldUpdate()) {
    return playingSound_ || previewTone_ != note::NONE;
  }

  if (previewTone_ != note::NONE) {
    if (static_cast<int32_t>(previewToneUntilMs_ - millis()) > 0) {
      setVolume(varioVolume_);
      speaker_driver::playTone(previewTone_);
      return true;
    }
    previewTone_ = note::NONE;
    speaker_driver::playTone(0);
  }

  updateVarioTest();

  if (playingSound_ && fxVolume_ != SpeakerVolume::Off) {
    // prioritize sound effects from UI & Button etc before we get to vario beeps
    // but only play soundFX if system volume is on
    return updateSound();

  } else if (vario_.note() != note::NONE && varioVolume_ != SpeakerVolume::Off) {
    // if there's a vario note to play, and the vario volume isn't zero
    updateVario();
    return false;

  } else {
    // play silence
    speaker_driver::playTone(0);
  }

  return false;
}

bool Speaker::shouldUpdate() {
  const uint32_t n = vario_audio::advanceFrame(millis(), tLastUpdate_);
  if (!n) return false;
  if (n >= 2) Serial.printf("Speaker::update skipped %d intervals\n", n - 1);
  return true;
}

bool Speaker::updateSound() {
  setVolume(fxVolume_);
  if (*soundPlaying_ != note::END) {
    speaker_driver::playTone(*soundPlaying_);
    fxNoteLast_ = *soundPlaying_;  // save last note

    // if we've played this note for enough samples
    if (++fxSampleCount_ >= FX_NOTE_SAMPLE_COUNT) {
      soundPlaying_++;
      fxSampleCount_ = 0;  // and reset sample count
    }
    return true;

  } else {  // Else, we're at END_OF_TONE
    speaker_driver::playTone(0);
    playingSound_ = false;
    fxNoteLast_ = note::NONE;
    return false;
  }
}

void Speaker::updateVario() {
  setVolume(varioVolume_);
  const bool write = vario_.resting() || vario_.smoothed() == note::NONE;
  const note::note_t tone = vario_.frame();
  if (write) speaker_driver::playTone(tone);
}

void Speaker::updateVarioSmoothing() {
  if (!varioToneActive() || !vario_.smoothed() || vario_.smoothed() == vario_.note()) return;
  speaker_driver::playTone(vario_.smooth());
}

bool Speaker::varioToneActive() const {
  return !speakerMute_ && !playingSound_ && previewTone_ == note::NONE &&
         varioVolume_ != SpeakerVolume::Off && vario_.active();
}

void Speaker::onUnexpectedState(const char* action, State actual) const {
  fatalError("%s while %s", action, nameOf(actual));
}
