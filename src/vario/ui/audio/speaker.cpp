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

  tLastUpdate_ = millis();
  state_ = State::Active;
  setVolume(varioVolume_);
}

void Speaker::mute() {
  assertState("Speaker::mute", State::Uninitialized, State::Active);
  updateVarioNote(0);  // ensure we clear vario note
  if (state_ != State::Uninitialized) {
    ledcWriteTone(SPEAKER_PIN, 0);  // mute speaker pin
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
    fatalError("Channel %s invalid in Speaker::setVolume", nameOf(channel));
  }
}

void Speaker::setVolume(SpeakerVolume volume) {
  assertState("Speaker::setVolume", State::Active);

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

  // don't play any beeps if Quiet Mode is turned on, and we haven't started a flight
  if (settings.vario_quietMode && !flightTimer_isRunning()) {
    varioNote_ = note::NONE;
    return;
  }

  uint16_t newVarioNote;
  uint16_t newVarioPlaySamples;
  uint16_t newVarioRestSamples;

  int sinkAlarm_cms;
  if (settings.vario_sinkAlarm_units) {
    sinkAlarm_cms = settings.vario_sinkAlarm * 100 / 196.85;  // convert fpm to cm/s
  } else {
    sinkAlarm_cms = settings.vario_sinkAlarm * 100;  // convert m/s to cm/s
  }

  if (verticalRate > settings.vario_climbStart) {
    // first clamp to thresholds if climbRate is over the max
    if (verticalRate >= CLIMB_MAX) {
      newVarioNote = verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      if (newVarioNote > CLIMB_NOTE_MAXMAX) newVarioNote = CLIMB_NOTE_MAXMAX;
      newVarioPlaySamples = CLIMB_PLAY_SAMPLES_MIN;
      newVarioRestSamples = 0;  // just hold a continuous tone, no rest in between
    } else {
      newVarioNote = verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      newVarioPlaySamples =
          CLIMB_PLAY_SAMPLES_MAX -
          (verticalRate * (CLIMB_PLAY_SAMPLES_MAX - CLIMB_PLAY_SAMPLES_MIN) / CLIMB_MAX);
      newVarioRestSamples =
          CLIMB_REST_SAMPLES_MAX -
          (verticalRate * (CLIMB_REST_SAMPLES_MAX - CLIMB_REST_SAMPLES_MIN) / CLIMB_MAX);
    }

    // if we trigger sink threshold
  } else if (verticalRate < sinkAlarm_cms) {
    // first clamp to thresholds if sinkRate is over the max
    if (verticalRate <= SINK_MAX) {
      newVarioNote = SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      if (newVarioNote < SINK_NOTE_MAXMAX || newVarioNote > SINK_NOTE_MAX)
        newVarioNote = SINK_NOTE_MAXMAX;  // the second condition (|| > SINK_NOTE_MAX) is to prevent
                                          // uint16 wrap-around to a much higher number
      newVarioPlaySamples = SINK_PLAY_SAMPLES_MAX;
      newVarioRestSamples = 0;  // just hold a continuous tone, no pulses
    } else {
      newVarioNote = SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      newVarioPlaySamples =
          SINK_PLAY_SAMPLES_MIN +
          (verticalRate * (SINK_PLAY_SAMPLES_MAX - SINK_PLAY_SAMPLES_MIN) / SINK_MAX);
      newVarioRestSamples =
          SINK_REST_SAMPLES_MIN +
          (verticalRate * (SINK_REST_SAMPLES_MAX - SINK_REST_SAMPLES_MIN) / SINK_MAX);
    }

  } else {
    varioNote_ = note::NONE;
  }

  varioNote_ = newVarioNote;
  varioPlaySamples_ = newVarioPlaySamples;
  varioRestSamples_ = newVarioRestSamples;
}

void Speaker::debugPrint() {
  static int microsLast = 0;
  static int millisLast = 0;

  int timeNowMillis = millis();
  int timeNowMicros = micros();

  Serial.print(timeNowMillis);
  Serial.print(" : ");
  Serial.print(timeNowMillis - millisLast);
  microsLast = timeNowMicros;
  millisLast = timeNowMillis;

  Serial.print("  SPKR UPDT -- Note: ");
  Serial.print(varioNote_);
  Serial.print(" Play: ");

  Serial.print(varioPlaySamples_);

  Serial.print(" Rest: ");
  Serial.print(varioRestSamples_);
  Serial.println(" !");
}

bool Speaker::update() {
  if (state_ == State::Uninitialized) {
    init();
  } else if (state_ != State::Active) {
    fatalError("Unsupported Speaker::update state %d", nameOf(state_));
  }

  unsigned long tNow = millis();
  if (tNow - tLastUpdate_ < NOTE_DURATION_MS - TIMING_TOLERANCE_MS) {
    // Nothing to do yet; we need to wait longer.
    // ...but return true if a sound is still playing
    return playingSound_;
  }
  tLastUpdate_ += NOTE_DURATION_MS;

  // only play sound if speaker is not muted
  if (speakerMute_) {
    return false;
  }

  // prioritize sound effects from UI & Button etc before we get to vario beeps
  // but only play soundFX if system volume is on
  if (playingSound_ && fxVolume_ != SpeakerVolume::Off) {
    setVolume(fxVolume_);
    if (*soundPlaying_ != note::END) {
      if (*soundPlaying_ != fxNoteLast_) {
        // only change pwm if it's a different note, otherwise we get a little audio blip between
        // the same notes
        ledcWriteTone(SPEAKER_PIN, *soundPlaying_);
      }
      fxNoteLast_ = *soundPlaying_;  // save last note

      // if we've played this note for enough samples
      if (++fxSampleCount_ >= FX_NOTE_SAMPLE_COUNT) {
        soundPlaying_++;
        fxSampleCount_ = 0;  // and reset sample count
      }
      return true;

    } else {  // Else, we're at END_OF_TONE
      ledcWriteTone(SPEAKER_PIN, 0);
      playingSound_ = false;
      fxNoteLast_ = note::NONE;
      setVolume(varioVolume_);  // return to vario volume in prep for beeps
      return false;
    }

    // if there's a vario note to play, and the vario volume isn't zero
  } else if (varioNote_ != note::NONE && varioVolume_ != SpeakerVolume::Off) {
    //  Handle the beeps and rests of a vario sound "measure"
    if (betweenVarioBeeps_) {
      ledcWriteTone(SPEAKER_PIN, 0);  // "play" silence since we're resting between beeps

      // stop playing rest if we've done it long enough
      if (++varioRestSampleCount_ >= varioRestSamples_) {
        varioRestSampleCount_ = 0;
        varioNoteLast_ = note::NONE;
        betweenVarioBeeps_ = false;  // next time through we want to play sound
      }

    } else {
      if (varioNote_ != varioNoteLast_) {
        // play the note, but only if different, otherwise we get a little audio blip between the
        // same successive notes (happens when vario is pegged and there's no rest period between)
        ledcWriteTone(SPEAKER_PIN, varioNote_);
      }
      varioNoteLast_ = varioNote_;

      if (++varioPlaySampleCount_ >= varioPlaySamples_) {
        varioPlaySampleCount_ = 0;
        if (varioRestSamples_)
          betweenVarioBeeps_ = true;  // next time through we want to play sound
      }
    }
  } else {
    // play silence (if timer is configured for auto-reload, we have to do this here because
    // sound_varioNote might have been set to 0 while we were beeping, and then we'll keep beeping)
    ledcWriteTone(SPEAKER_PIN, 0);
  }

  return false;
}

void Speaker::onUnexpectedState(const char* action, State actual) const {
  fatalError("%s while %s", action, nameOf(actual));
}
