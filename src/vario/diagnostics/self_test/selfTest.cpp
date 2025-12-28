#include "selfTest.h"

#include <FS.h>
#include <SD_MMC.h>

#include "hardware/buttons.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "power.h"
#include "storage/sd_card.h"
#include "system/version_info.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/input/buttons.h"

SelfTest selfTest;
ButtonsInteractiveTest buttonsTest;

constexpr size_t BUFFER_SIZE = 512;
File self_test_file;

bool useSDFile() {
  if (self_test_file) {
    return true;
  }

  unsigned int i = 1;
  char fileName[32];

  while (true) {
    snprintf(fileName, sizeof(fileName), "/self_test_%u.txt", i);

    fs::File f = SD_MMC.open(fileName, "r");
    if (!f) {
      // If the error was that the file doesn't exist, break and use this name
      break;
    }

    f.close();  // File exists, close and try next
    i++;
    if (i > 1000) {
      // Avoid infinite loop in case of filesystem issues
      return false;
    }
  }

  self_test_file = SD_MMC.open(fileName, "w", true);  // open for writing, create if doesn't exist

  // Write the version information to know what generated this fatal error
  self_test_file.print("Hardware Variant: ");
  self_test_file.println(LeafVersionInfo::hardwareVariant());
  self_test_file.print("Firmware Version: ");
  self_test_file.println(LeafVersionInfo::firmwareVersion());

  return self_test_file;
}

void selfTestInfo(const char* msg, ...) {
  char buffer[BUFFER_SIZE];

  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, BUFFER_SIZE, msg, args);
  va_end(args);

  Serial.println(buffer);
  if (useSDFile()) {
    self_test_file.print("Info: ");
    self_test_file.println(buffer);
  }
}

/////////////////////////////////////////////////////////////
// Implementations of individual test functions
//

// Test SD Card first since test results will be logged there
SelfTest::Status SelfTest::testSDCard() {
  Status result = Status::Unknown;  // default Unknown
  if (sdcard.isCardPresent() == false) {
    selfTestInfo("* SELF TEST * SD CARD * WARNING - Physical card detection failed");
  }
  if (!sdcard.isMounted()) {
    selfTestInfo("* SELF TEST * SD CARD * FAIL - SD Card not mounted");
    result = Status::Fail;
  } else if (!useSDFile()) {
    selfTestInfo("* SELF TEST * SD CARD * WARNING - Cannot save self test results");
    result = Status::Fail;
  } else {
    selfTestInfo("* SELF TEST * SD CARD * PASS - Saving self test results");
    result = Status::Pass;
  }
  return result;
}

SelfTest::Status SelfTest::testBaro() {
  Status result = Status::Unknown;  // default Unknown
  if (baro.state() != Barometer::State::Ready) {
    selfTestInfo("* SELF TEST *  BARO   * FAIL - Barometer not ready");
    result == Status::Fail;
  } else {
    if (baro.altF() < 3500 && baro.altF() > -200) {
      Serial.println("* SELF TEST *  BARO   * PASS");
      result = Status::Pass;
    } else {
      selfTestInfo("* SELF TEST *  BARO   * FAIL - Value out of range: %g", baro.altF());
      result == Status::Fail;
    }
  }
  return result;
}

SelfTest::Status SelfTest::testIMU() {
  Status result = Status::Unknown;  // default Unknown
  if (!imu.accelValid()) {
    selfTestInfo("* SELF TEST *   IMU   * FAIL - IMU not ready");
    result = Status::Fail;
  } else {
    float accelTotal = imu.getAccel();
    // Check if acceleration values are within a reasonable range
    if (accelTotal < 1.2f && accelTotal > 0.8f) {
      result = Status::Pass;
      selfTestInfo("* SELF TEST *   IMU   * PASS");
    } else {
      selfTestInfo("* SELF TEST *   IMU   * FAIL - Total acceleration out of range: %g",
                   accelTotal);
      result = Status::Fail;
    }
  }
  return result;
}

SelfTest::Status SelfTest::testAmbient() {
  Status result = Status::Unknown;  // default Unknown
  if (ambient.state() != Ambient::State::Ready) {
    selfTestInfo("* SELF TEST * AMBIENT * FAIL - Ambient sensor not ready");
    result = Status::Fail;
  } else {
    float temperature = ambient.temp();
    if (temperature < 60.0f && temperature > -5.0f) {
      result = Status::Pass;
      selfTestInfo("* SELF TEST * AMBIENT * PASS");
    } else {
      selfTestInfo("* SELF TEST * AMBIENT * Ambient temperature out of range: %g", temperature);
    }
  }
  return result;
}

SelfTest::Status SelfTest::testGPS() {
  Status result = Status::Unknown;  // default Unknown
  if (!gps.fixInfo.numberOfSats) {
    selfTestInfo("* SELF TEST *   GPS   * FAIL - GPS sees zero satellites");
    result = Status::Fail;
  } else {
    result = Status::Pass;
    selfTestInfo("* SELF TEST *   GPS   * PASS - GPS sees %d satellites", gps.fixInfo.numberOfSats);
  }
  return result;
}

SelfTest::Status SelfTest::testDisplay() {
  Status result = SelfTest::Status::Unknown;  // Default Unknown
  return result;
}

///////////////////////////////////////////////
// Button Test

bool ButtonsInteractiveTest::update() {
  if (!running) {
    running = true;
    selfTestInfo("* SELF TEST * BUTTONS * Starting button test - please press each button");
    result = SelfTest::Status::Unknown;
    selfTest_pageButtons.show();  // show display page for button test
  }

  Button button = buttons.inspectPins();
  if (button == Button::UP && !upPressed) {
    upPressed = true;
    Serial.println("* SELF TEST * BUTTONS * UP button detected");
    display.update();
  } else if (button == Button::DOWN && !downPressed) {
    downPressed = true;
    Serial.println("* SELF TEST * BUTTONS * DOWN button detected");
    display.update();
  } else if (button == Button::LEFT && !leftPressed) {
    leftPressed = true;
    Serial.println("* SELF TEST * BUTTONS * LEFT button detected");
    display.update();
  } else if (button == Button::RIGHT && !rightPressed) {
    rightPressed = true;
    Serial.println("* SELF TEST * BUTTONS * RIGHT button detected");
    display.update();
  } else if (button == Button::CENTER && !centerPressed) {
    centerPressed = true;
    Serial.println("* SELF TEST * BUTTONS * CENTER button detected");
    display.update();
  }

  // Test fails if timeout before all buttons pushed
  if (buttonsTest.waitForInput-- <= 0) {
    buttonsTest.result = SelfTest::Status::Fail;
    speaker.playSound(fx::cancel);
    selfTestInfo("* SELF TEST * BUTTONS * FAIL - Timeout waiting for button presses");
    if (!upPressed) {
      Serial.println("* SELF TEST * BUTTONS * FAIL - UP button NOT DETECTED");
    }
    if (!downPressed) {
      Serial.println("* SELF TEST * BUTTONS * FAIL - DOWN button NOT DETECTED");
    }
    if (!leftPressed) {
      Serial.println("* SELF TEST * BUTTONS * FAIL - LEFT button NOT DETECTED");
    }
    if (!rightPressed) {
      Serial.println("* SELF TEST * BUTTONS * FAIL - RIGHT button NOT DETECTED");
    }
    if (!centerPressed) {
      Serial.println("* SELF TEST * BUTTONS * FAIL - CENTER button NOT DETECTED");
    }
  }

  // Test passes if all buttons have been pressed
  if (upPressed && downPressed && leftPressed && rightPressed && centerPressed) {
    buttonsTest.result = SelfTest::Status::Pass;
    speaker.playSound(fx::confirm);
    selfTestInfo("* SELF TEST * BUTTONS * PASS - All buttons detected");
    // delay(750);  // pause to let user see success on display screen
  }

  // handle test results (or continue test)
  if (buttonsTest.result != SelfTest::Status::Unknown) {
    Serial.println("* SELF TEST * BUTTONS * Test complete");
    selfTest.results.buttons = buttonsTest.result;
    display.update();
    delay(500);                    // pause to let user see test results on display screen
    selfTest_pageButtons.close();  // close button test display page
    // test complete, reset variables and stop running the test
    upPressed = false;
    downPressed = false;
    leftPressed = false;
    rightPressed = false;
    centerPressed = false;
    waitForInput = 800;  // reset timeout (10ms ticks)
    running = false;
  }

  return running;
}

///////////////////////////////////////////////
// Power Test

SelfTest::Status SelfTest::testPower() {
  // Placeholder implementation
  return Status::Pass;
}

SelfTest::Status SelfTest::testSpeaker() {
  Status result = Status::Unknown;  // default Unknown

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
      result = Status::Pass;
      break;
    } else if (button == Button::DOWN || button == Button::LEFT) {
      result = Status::Fail;
      break;
    }
    delay(1);
  }

  return result;
}

void SelfTest::runAllTests() {
  if (buttonsTest.result == SelfTest::Status::Unknown) {
    buttonsTest.update();
  }
  // else if (other tests go here)
  else {
    selfTest.running = false;
    Serial.println("* SELF TEST * All tests complete");
  }
  // runAutoTests(false);        // keep file open
  // runInteractiveTests(true);  // close file when done
}

void SelfTest::runAutoTests(bool closeFileWhenDone) {
  selfTest.results.baro = testBaro();
  selfTest.results.imu = testIMU();
  selfTest.results.gps = testGPS();
  selfTest.results.ambient = testAmbient();
  selfTest.results.sdCard = testSDCard();
  selfTest.results.power = testPower();
  if (closeFileWhenDone && self_test_file) {
    self_test_file.close();
  }
}

void SelfTest::runInteractiveTests(bool closeFileWhenDone) {
  selfTest.results.buttons = testButtons();
  selfTest.results.speaker = testSpeaker();
  selfTest.results.display = testDisplay();
  selfTest.results.kalman = testKalman();
  if (closeFileWhenDone && self_test_file) {
    self_test_file.close();
  }
}

void SelfTest::clearResults() {
  buttonsTest.result = SelfTest::Status::Unknown;
  selfTest.results.buttons = SelfTest::Status::Unknown;
}