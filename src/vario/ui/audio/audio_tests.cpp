#include "ui/audio/audio_tests.h"

#include <Arduino.h>

#include "ui/audio/notes.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"

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
    note::note_t n = 0;
    switch (letter) {
      case 'z':
        n = note::A2;
        break;
      case 'x':
        n = note::B2;
        break;
      case 'c':
        n = note::C3;
        break;
      case 'v':
        n = note::D3;
        break;
      case 'b':
        n = note::E3;
        break;
      case 'n':
        n = note::F3;
        break;
      case 'm':
        n = note::G3;
        break;
      case ',':
        n = note::A3;
        break;
      case '.':
        n = note::B3;
        break;
      case 'a':
        n = note::A3;
        break;
      case 's':
        n = note::B3;
        break;
      case 'd':
        n = note::C4;
        break;
      case 'f':
        n = note::D4;
        break;
      case 'g':
        n = note::E4;
        break;
      case 'h':
        n = note::F4;
        break;
      case 'j':
        n = note::G4;
        break;
      case 'k':
        n = note::A4;
        break;
      case 'l':
        n = note::B4;
        break;
      case ';':
        n = note::C5;
        break;
      case 'q':
        n = note::A4;
        break;
      case 'w':
        n = note::B4;
        break;
      case 'e':
        n = note::C5;
        break;
      case 'r':
        n = note::D5;
        break;
      case 't':
        n = note::E5;
        break;
      case 'y':
        n = note::F5;
        break;
      case 'u':
        n = note::G5;
        break;
      case 'i':
        n = note::A5;
        break;
      case 'o':
        n = note::B5;
        break;
      case 'p':
        n = note::C6;
        break;
      case '[':
        n = note::D6;
        break;
      case ']':
        n = note::E6;
        break;
      case '0':
        speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Off);
        break;
      case '1':
        speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Low);
        break;
      case '2':
        speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Medium);
        break;
      case '3':
        speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::High);
        break;
      case '4':
        speaker.playSound(fx::buttonpress);
        break;
      case '5':
        speaker.playSound(fx::buttonhold);
        break;
      case '6':
        speaker.playSound(fx::confirm);
        break;
      case '7':
        speaker.playSound(fx::goingdown);
        break;
      case '8':
        speaker.playSound(fx::octavesup);
        break;
      case '9':
        speaker.playSound(fx::octavesdown);
        break;
    }
    if (n) speaker.playNote(n);
  }
}
