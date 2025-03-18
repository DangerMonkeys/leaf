#include "speaker.h"

#include <Arduino.h>

#include "Leaf_SPI.h"
#include "configuration.h"
#include "io_pins.h"
#include "log.h"
#include "settings.h"

uint8_t speakerVolume = 1;
bool speakerMute = false;  // use to mute sound for varius charging & sleep states

// Sound Effects
uint16_t fx_silence[] = {NOTE_END};

uint16_t fx_increase[] = {NOTE_C4, NOTE_G4, NOTE_END};
uint16_t fx_decrease[] = {NOTE_C4, NOTE_F3, NOTE_END};  // 110 140, END_OF_TONE};
uint16_t fx_neutral[] = {NOTE_C4, NOTE_C4, NOTE_END};   // 110, 110, END_OF_TONE};
uint16_t fx_neutralLong[] = {
    NOTE_C4, NOTE_C4, NOTE_C4, NOTE_C4, NOTE_C4,
    NOTE_C4, NOTE_C4, NOTE_C4, NOTE_END};  //  {110, 110, 110, 110, 110, 110, 110, 110,10, 110,
                                           //  1110, 110, 110, 110, 110, 110, 110, 110, 110, 110,
                                           //  END_OF_TONE};
uint16_t fx_double[] = {NOTE_C4, NOTE_NONE, NOTE_C4, NOTE_END};  // 110, 0, 110, END_OF_TONE};

uint16_t fx_enter[] = {NOTE_A4, NOTE_C4, NOTE_E4, NOTE_END};  // 150, 120, 90, END_OF_TONE};
uint16_t fx_exit[] = {NOTE_C5, NOTE_A5, NOTE_F4, NOTE_C4,
                      NOTE_END};  // 65, 90, 120, 150, END_OF_TONE};
uint16_t fx_confirm[] = {NOTE_C3, NOTE_G3, NOTE_B3, NOTE_C4, NOTE_END};
uint16_t fx_cancel[] = {NOTE_C4, NOTE_G4, NOTE_C3, NOTE_END};
uint16_t fx_on[] = {250, 200, 150, 100, 50, NOTE_END};
uint16_t fx_off[] = {50, 100, 150, 200, 250, NOTE_END};

uint16_t fx_buttonpress[] = {NOTE_F3, NOTE_A3, NOTE_C4, NOTE_END};
uint16_t fx_buttonhold[] = {150, 200, 250, NOTE_END};
uint16_t fx_goingup[] = {55, 54, 53, 52, 51, 50, 49, 47, 44, 39, NOTE_END};
uint16_t fx_goingdown[] = {31, 31, 32, 33, 34, 35, 36, 38, 41, 46, NOTE_END};
uint16_t fx_octavesup[] = {45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, NOTE_END};
uint16_t fx_octavesdown[] = {31, 31, 40, 40, 45, 45, 65, 65, 90, 90, NOTE_END};

uint16_t single_note[] = {
    0, NOTE_END};  // this is to allow playing single notes by changing random_note[0], while still
                   // having a NOTE_END terminator following.

// volatile pointer to the sound sample to play
volatile uint16_t* snd_index;

// notes we should play, and if we're currently playing
volatile uint16_t sound_varioNote = 0;      // note to play for vario beeps
volatile uint16_t sound_varioNoteLast = 0;  // last note played for vario beeps
volatile uint16_t sound_fxNoteLast = 0;     // last note played for sound effects
volatile bool sound_varioResting = 0;       // are we resting (silence) between beeps?
volatile bool sound_currentlyPlaying = 0;   // are we playing a sound?
volatile bool sound_fx = 0;  // track if we have any sound effects to play (button presses, etc)

// trackers for fixed-sample length speaker timer approach #1
volatile uint16_t sound_vario_play_samples =
    CLIMB_PLAY_SAMPLES_MAX;  // amount of samples we should play for
volatile uint16_t sound_vario_rest_samples =
    CLIMB_REST_SAMPLES_MAX;  // amount of samples we should rest for
volatile uint8_t sound_vario_play_sample_count =
    0;  // track how many samples (beats) we've played per note when playing sound effects (using
        // method #1 -- fixed sample length)
volatile uint8_t sound_vario_rest_sample_count =
    0;  // track how many samples (beats) we've played per note when playing sound effects (using
        // method #1 -- fixed sample length)
volatile uint8_t sound_fx_sample_count =
    0;  // track how many samples (beats) we've played per note when playing sound effects (using
        // method #1 -- fixed sample length)

// trackers for adjustable length speaker timer approach #2
volatile uint16_t sound_varioNoteLastUpdate = 0;           // last note updated in climb analysis
volatile uint16_t sound_varioPlayLength = CLIMB_PLAY_MAX;  //
volatile uint16_t sound_varioRestLength = CLIMB_REST_MAX;  //
//

void speaker_init(void) {
  // configure speaker pinout and PWM channel
  pinMode(SPEAKER_PIN, OUTPUT);
  ledcAttach(SPEAKER_PIN, 1000, 10);

  // set speaker volume pins as outputs IF NOT ON the IO Expander
  if (!SPEAKER_VOLA_IOEX) pinMode(SPEAKER_VOLA, OUTPUT);
  if (!SPEAKER_VOLB_IOEX) pinMode(SPEAKER_VOLB, OUTPUT);

  snd_index = fx_silence;
}

void speaker_mute() {
  speaker_updateVarioNote(0);     // ensure we clear vario note
  ledcWriteTone(SPEAKER_PIN, 0);  // mute speaker pin
  speakerMute = true;
}

void speaker_unMute() { speakerMute = false; }

void speaker_setDefaultVolume() { speaker_setVolume(1); }

void speaker_incVolume() {
  if (++speakerVolume > 3) speakerVolume = 3;
  speaker_setVolume(speakerVolume);
}

void speaker_decVolume() {
  if (speakerVolume == 0) {
  }  // do nothing
  else
    speakerVolume--;
  speaker_setVolume(speakerVolume);
}

void speaker_setVolume(unsigned char volume) {
  if (SPEAKER_VOLA == NC || SPEAKER_VOLB == NC) return;
  speakerVolume = volume;
  switch (volume) {
    case 0:  // No Volume -- disable piezo speaker driver
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 0);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 0);
      break;
    case 1:  // Low Volume -- enable piezo speaker driver 1x (3.3V)
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 1);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 0);
      break;
    case 2:  // Med Volume -- enable piezo speaker driver 2x (6.6V)
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 0);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 1);
      break;
    case 3:  // High Volume -- enable piezo spaker driver 3x (9.9V)
      ioexDigitalWrite(SPEAKER_VOLA_IOEX, SPEAKER_VOLA, 1);
      ioexDigitalWrite(SPEAKER_VOLB_IOEX, SPEAKER_VOLB, 1);
      break;
  }
}

void speaker_playSound(uint16_t* sound) {
  snd_index = sound;
  Serial.print("setting snd_index to: ");
  Serial.print(*snd_index);
  Serial.print(" @ ");
  Serial.println(millis());
  sound_fx = 1;
}

void speaker_playNote(uint16_t note) {
  single_note[0] = note;
  snd_index = single_note;
  sound_fx = 1;
}

unsigned char climbToneSpread;
unsigned int climbRateSpread;
unsigned int climbPlaySpread;
unsigned int climbSilenceSpread;

unsigned char sinkToneSpread;
unsigned int sinkRateSpread;
unsigned int sinkPlaySpread;
unsigned int sinkSilenceSpread;

unsigned char liftyToneSpread;
unsigned int liftyRateSpread;
unsigned int liftySilenceSpread;
unsigned char liftyAirGap =
    1;  // track if we're playing the gap between double-beeps or the longer silence
unsigned char liftyAirNow = 0;  // track if we're playing lifty air tone

void speaker_updateVarioNote(int32_t verticalRate) {
  // don't play any beeps if Quiet Mode is turned on, and we haven't started a flight
  if (settings.vario_quietMode && !flightTimer_isRunning()) {
    sound_varioNote = 0;
    return;
  }

  sound_varioNoteLastUpdate = sound_varioNote;

  uint16_t sound_varioNoteTEMP;
  uint16_t sound_vario_play_samplesTEMP;
  uint16_t sound_vario_rest_samplesTEMP;

  if (verticalRate > settings.vario_climbStart) {
    // first clamp to thresholds if climbRate is over the max
    if (verticalRate >= CLIMB_MAX) {
      sound_varioNoteTEMP =
          verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      if (sound_varioNoteTEMP > CLIMB_NOTE_MAXMAX) sound_varioNoteTEMP = CLIMB_NOTE_MAXMAX;
      sound_vario_play_samplesTEMP = CLIMB_PLAY_SAMPLES_MIN;
      sound_vario_rest_samplesTEMP = 0;  // just hold a continuous tone, no rest in between
    } else {
      sound_varioNoteTEMP =
          verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      sound_vario_play_samplesTEMP =
          CLIMB_PLAY_SAMPLES_MAX -
          (verticalRate * (CLIMB_PLAY_SAMPLES_MAX - CLIMB_PLAY_SAMPLES_MIN) / CLIMB_MAX);
      sound_vario_rest_samplesTEMP =
          CLIMB_REST_SAMPLES_MAX -
          (verticalRate * (CLIMB_REST_SAMPLES_MAX - CLIMB_REST_SAMPLES_MIN) / CLIMB_MAX);
    }

    // if we trigger sink threshold
  } else if (verticalRate <
             (settings.vario_sinkAlarm * 100)) {  // convert sink alarm 'm/s' setting to cm/s
    // first clamp to thresholds if sinkRate is over the max
    if (verticalRate <= SINK_MAX) {
      sound_varioNoteTEMP =
          SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      if (sound_varioNoteTEMP < SINK_NOTE_MAXMAX || sound_varioNoteTEMP > SINK_NOTE_MAX)
        sound_varioNoteTEMP =
            SINK_NOTE_MAXMAX;  // the second condition (|| > SINK_NOTE_MAX) is to prevent uint16
                               // wrap-around to a much higher number
      sound_vario_play_samplesTEMP = SINK_PLAY_SAMPLES_MAX;
      sound_vario_rest_samplesTEMP = 0;  // just hold a continuous tone, no pulses
    } else {
      sound_varioNoteTEMP =
          SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      sound_vario_play_samplesTEMP =
          SINK_PLAY_SAMPLES_MIN +
          (verticalRate * (SINK_PLAY_SAMPLES_MAX - SINK_PLAY_SAMPLES_MIN) / SINK_MAX);
      sound_vario_rest_samplesTEMP =
          SINK_REST_SAMPLES_MIN +
          (verticalRate * (SINK_REST_SAMPLES_MAX - SINK_REST_SAMPLES_MIN) / SINK_MAX);
    }

  } else {
    sound_varioNote = 0;
  }

  sound_varioNote = sound_varioNoteTEMP;
  sound_vario_play_samples = sound_vario_play_samplesTEMP;
  sound_vario_rest_samples = sound_vario_rest_samplesTEMP;
}

int microsLast = 0;
int millisLast = 0;

void speaker_debugPrint() {
  int timeNowMillis = millis();
  int timeNowMicros = micros();

  Serial.print(timeNowMillis);
  Serial.print(" : ");
  Serial.print(timeNowMillis - millisLast);
  microsLast = timeNowMicros;
  millisLast = timeNowMillis;

  Serial.print("  SPKR UPDT -- Note: ");
  Serial.print(sound_varioNote);
  Serial.print(" Play: ");

  Serial.print(sound_vario_play_samples);

  Serial.print(" Rest: ");
  Serial.print(sound_vario_rest_samples);
  Serial.println(" !");
}

// count timer cycles so we only update note every 4th time
uint8_t speakerTimerCount = 0;

// Run every 10ms from main loop to adjust tone if needed
bool onSpeakerTimer() {
  // return true if there are notes left to play
  bool notesLeftToPlay = false;

  // our 'samples' (note length) are 40ms, so only do this every 4th time
  if (++speakerTimerCount >= 4) {
    speakerTimerCount = 0;
    // proceed with adjusting the tone
  } else {
    // don't do anything this time, but return true if sound_fx is still playing
    if (sound_fx) {
      notesLeftToPlay = true;
    }
    return notesLeftToPlay;
  }

  // only play sound if speaker is not muted
  if (speakerMute) {
    ledcWriteTone(SPEAKER_PIN, 0);
    return notesLeftToPlay;
  }

  // prioritize sound effects from UI & Button etc before we get to vario beeps
  // but only play soundFX if system volume is on
  if (sound_fx && settings.system_volume) {
    notesLeftToPlay = true;
    speaker_setVolume(
        settings.system_volume);  // play system sound effects at system volume setting

    // Serial.print("FX: "); Serial.print(*snd_index); Serial.print(" @ ");
    // Serial.println(millis());
    if (*snd_index != NOTE_END) {
      if (*snd_index != sound_fxNoteLast)
        ledcWriteTone(SPEAKER_PIN,
                      *snd_index);  // only change pwm if it's a different note, otherwise we get a
                                    // little audio blip between the same notes
      sound_fxNoteLast = *snd_index;  // save last note and move on to next

      // if we've played this note for enough samples
      if (++sound_fx_sample_count >= FX_NOTE_SAMPLE_COUNT) {
        snd_index++;
        sound_fx_sample_count = 0;  // and reset sample count
      }

    } else {  // Else, we're at END_OF_TONE
      ledcWriteTone(SPEAKER_PIN, 0);
      sound_fx = 0;
      sound_fxNoteLast = 0;
      notesLeftToPlay = false;
      speaker_setVolume(settings.vario_volume);  // return to vario volume in prep for beeps
    }

    // if there's a vario note to play, and the vario volume isn't zero
  } else if (sound_varioNote > 0 && settings.vario_volume) {
    // commenting this line out; instead let's return to vario volume at the end of SoundFX
    // speaker_setVolume( settings.vario_volume);  // play vario sounds at vario volume setting

    //  Handle the beeps and rests of a vario sound "measure"
    if (sound_varioResting) {
      ledcWriteTone(SPEAKER_PIN, 0);  // "play" silence since we're resting between beeps

      // stop playing rest if we've done it long enough
      if (++sound_vario_rest_sample_count >= sound_vario_rest_samples) {
        sound_vario_rest_sample_count = 0;
        sound_varioNoteLast = 0;
        sound_varioResting = false;  // next time through we want to play sound
      }

    } else {
      // Serial.println("  VAR_BEEP");
      if (sound_varioNote != sound_varioNoteLast)
        ledcWriteTone(
            SPEAKER_PIN,
            sound_varioNote);  // play the note, but only if different, otherwise we get a little
                               // audio blip between the same successive notes (happens when vario
                               // is pegged and there's no rest period between)
      sound_varioNoteLast = sound_varioNote;

      if (++sound_vario_play_sample_count >= sound_vario_play_samples) {
        sound_vario_play_sample_count = 0;
        if (sound_vario_rest_samples)
          sound_varioResting = true;  // next time through we want to play sound
      }
    }
  } else {
    // Serial.println("ISR NO SOUND");
    ledcWriteTone(SPEAKER_PIN, 0);  // play silence (if timer is configured for auto-reload, we have
                                    // to do this here because sound_varioNote might have been set
                                    // to 0 while we were beeping, and then we'll keep beeping)
  }

  return notesLeftToPlay;
}

void speaker_TEST(void) {
  // Serial.println('0');

  // Explore sounds
  speaker_playPiano();

  // Test sound fx
  /*
  for (int i=0; i<20; i++) {
    sound_fx = 1;
    Serial.print("now called playSound()");
    Serial.println(i);
    speaker_playSound(i);
    delay(4000);
  }
  */

  // Test Vario Beeps
  /*
  for (int16_t i=11; i<900; i+=50) {
    speaker_updateVarioNote(i);
    delay(3000);
  }
  */
}

void speaker_playPiano(void) {
  if (Serial.available() > 0) {
    char letter = char(Serial.read());
    while (Serial.available() > 0) {
      Serial.read();
    }

    Serial.println(letter);
    int fx = 0;
    switch (letter) {
      case 'z':
        fx = NOTE_A2;
        break;
      case 'x':
        fx = NOTE_B2;
        break;
      case 'c':
        fx = NOTE_C3;
        break;
      case 'v':
        fx = NOTE_D3;
        break;
      case 'b':
        fx = NOTE_E3;
        break;
      case 'n':
        fx = NOTE_F3;
        break;
      case 'm':
        fx = NOTE_G3;
        break;
      case ',':
        fx = NOTE_A3;
        break;
      case '.':
        fx = NOTE_B3;
        break;
      case 'a':
        fx = NOTE_A3;
        break;
      case 's':
        fx = NOTE_B3;
        break;
      case 'd':
        fx = NOTE_C4;
        break;
      case 'f':
        fx = NOTE_D4;
        break;
      case 'g':
        fx = NOTE_E4;
        break;
      case 'h':
        fx = NOTE_F4;
        break;
      case 'j':
        fx = NOTE_G4;
        break;
      case 'k':
        fx = NOTE_A4;
        break;
      case 'l':
        fx = NOTE_B4;
        break;
      case ';':
        fx = NOTE_C5;
        break;
      case 'q':
        fx = NOTE_A4;
        break;
      case 'w':
        fx = NOTE_B4;
        break;
      case 'e':
        fx = NOTE_C5;
        break;
      case 'r':
        fx = NOTE_D5;
        break;
      case 't':
        fx = NOTE_E5;
        break;
      case 'y':
        fx = NOTE_F5;
        break;
      case 'u':
        fx = NOTE_G5;
        break;
      case 'i':
        fx = NOTE_A5;
        break;
      case 'o':
        fx = NOTE_B5;
        break;
      case 'p':
        fx = NOTE_C6;
        break;
      case '[':
        fx = NOTE_D6;
        break;
      case ']':
        fx = NOTE_E6;
        break;
      case '0':
        speaker_setVolume(0);
        break;
      case '1':
        speaker_setVolume(1);
        break;
      case '2':
        speaker_setVolume(2);
        break;
      case '3':
        speaker_setVolume(3);
        break;
      case '4':
        speaker_playSound(fx_buttonpress);
        break;
      case '5':
        speaker_playSound(fx_buttonhold);
        break;
      case '6':
        speaker_playSound(fx_confirm);
        break;
      case '7':
        speaker_playSound(fx_goingdown);
        break;
      case '8':
        speaker_playSound(fx_octavesup);
        break;
      case '9':
        speaker_playSound(fx_octavesdown);
        break;
    }
    if (fx) speaker_playNote(fx);
  }
}
