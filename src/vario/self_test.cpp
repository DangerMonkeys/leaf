#include "self_test.h"

#include "hardware/buttons.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/input/buttons.h"

SelfTest selfTest;

// Implementations of individual test functions
bool SelfTest::testBaro() {
  bool result = false;  // default fail
  if (baro.state() != Barometer::State::Ready) {
    Serial.println("* SELF TEST *  BARO   * Barometer not ready");
  } else {
    if (baro.altF() < 4000 && baro.altF() > -500) {
      result = true;
    } else {
      Serial.println("* SELF TEST *  BARO   * Barometer reading out of range");
    }
  }
  return result;
}

bool SelfTest::testIMU() {
  bool result = false;  // default fail
  if (!imu.accelValid()) {
    Serial.println("* SELF TEST *   IMU   * IMU not ready");
  } else {
    float accelTotal = imu.getAccel();
    // Check if acceleration values are within a reasonable range
    if (accelTotal < 1.2f && accelTotal > 0.8f) {
      result = true;
    } else {
      Serial.println("* SELF TEST *   IMU   * Total acceleration out of range");
    }
    return result;
  }
}

bool SelfTest::testAmbient() {
  bool result = false;  // default fail
  if (ambient.state() != Ambient::State::Ready) {
    Serial.println("* SELF TEST * AMBIENT * Ambient sensor not ready");
  } else {
    float temperature = ambient.temp();
    if (temperature < 60.0f && temperature > -5.0f) {
      result = true;
    } else {
      Serial.println("* SELF TEST * AMBIENT * Ambient temperature out of range");
    }
  }
  return true;
}

bool SelfTest::testGPS() {
  bool result = false;  // default fail
  if (!gps.fixInfo.numberOfSats) {
    Serial.println("* SELF TEST *   GPS   * GPS sees zero satellites");
  } else {
    result = true;
  }
}

bool SelfTest::testDisplay() {
  // Placeholder implementation
  return true;
}

bool SelfTest::testButtons() {
  bool result = false;  // default fail
  bool upPressed = false;
  bool downPressed = false;
  bool leftPressed = false;
  bool rightPressed = false;
  bool centerPressed = false;

  int waitForInput = 8000;  // wait up to 8 seconds for user input

  while (waitForInput-- > 0) {
    Button button = buttons.inspectPins();
    if (button == Button::UP) {
      upPressed = true;
      Serial.println("* SELF TEST * BUTTONS * UP button detected");
    } else if (button == Button::DOWN) {
      downPressed = true;
      Serial.println("* SELF TEST * BUTTONS * DOWN button detected");
    } else if (button == Button::LEFT) {
      leftPressed = true;
      Serial.println("* SELF TEST * BUTTONS * LEFT button detected");
    } else if (button == Button::RIGHT) {
      rightPressed = true;
      Serial.println("* SELF TEST * BUTTONS * RIGHT button detected");
    } else if (button == Button::CENTER) {
      centerPressed = true;
      Serial.println("* SELF TEST * BUTTONS * CENTER button detected");
    }
    delay(1);
  }

  if (!upPressed) {
    Serial.println("* SELF TEST * BUTTONS * UP button NOT DETECTED");
  }
  if (!downPressed) {
    Serial.println("* SELF TEST * BUTTONS * DOWN button NOT DETECTED");
  }
  if (!leftPressed) {
    Serial.println("* SELF TEST * BUTTONS * LEFT button NOT DETECTED");
  }
  if (!rightPressed) {
    Serial.println("* SELF TEST * BUTTONS * RIGHT button NOT DETECTED");
  }
  if (!centerPressed) {
    Serial.println("* SELF TEST * BUTTONS * CENTER button NOT DETECTED");
  }
  if (upPressed && downPressed && leftPressed && rightPressed && centerPressed) {
    result = true;
  }
  return result;
}

bool SelfTest::testSDCard() {
  // Placeholder implementation
  return true;
}

bool SelfTest::testPower() {
  // Placeholder implementation
  return true;
}

bool SelfTest::testSpeaker() {
  bool result = false;  // default fail

  speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Low);
  speaker.playSound(fx::doubleRise);
  while (speaker.update()) {
    delay(10);  // delay to let sound finish playing
  }
  delay(500);
  speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Medium);
  speaker.playSound(fx::doubleRise);
  while (speaker.update()) {
    delay(10);  // delay to let sound finish playing
  }
  delay(500);
  speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::High);
  speaker.playSound(fx::tripleRise);
  while (speaker.update()) {
    delay(10);  // delay to let sound finish playing
  }
  delay(500);
  speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Off);
  speaker.playSound(fx::quadRise);
  while (speaker.update()) {
    delay(10);  // delay to let sound finish playing
  }
  delay(500);

  int waitForInput = 5000;  // wait up to 5 seconds for user input
  Button button = Button::NONE;
  Serial.println(
      "* SELF TEST * SPEAKER * Hear 1, 2, and 3 beeps increasingly louder, AND not 4 beeps?");
  Serial.println("* SELF TEST * SPEAKER * YES = UP or RIGHT button, NO = DOWN or LEFT button");
  while (waitForInput-- > 0) {
    button = buttons.inspectPins();
    if (button == Button::UP || button == Button::RIGHT) {
      result = true;
      break;
    } else if (button == Button::DOWN || button == Button::LEFT) {
      result = false;
      break;
    }
    delay(1);
  }

  return result;
}

void SelfTest::runAllTests() {
  runAutoTests();
  runInteractiveTests();
}

void SelfTest::runAutoTests() {
  selfTest.results.baro = testBaro();
  selfTest.results.imu = testIMU();
  selfTest.results.gps = testGPS();
  selfTest.results.ambient = testAmbient();
  selfTest.results.sdCard = testSDCard();
  selfTest.results.power = testPower();
}

void SelfTest::runInteractiveTests() {
  selfTest.results.buttons = testButtons();
  selfTest.results.speaker = testSpeaker();
  selfTest.results.display = testDisplay();
}