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
VarioInteractiveTest varioTest;

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
  Status result = Status::Running;
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
  Status result = Status::Running;
  if (baro.state() != Barometer::State::Ready) {
    selfTestInfo("* SELF TEST *  BARO   * FAIL - Barometer not ready");
    result == Status::Fail;
  } else {
    if (baro.altF() < 3500 && baro.altF() > -200) {
      selfTestInfo("* SELF TEST *  BARO   * PASS - Barometer reading: %g", baro.altF());
      result = Status::Pass;
    } else {
      selfTestInfo("* SELF TEST *  BARO   * FAIL - Value out of range: %g", baro.altF());
      result == Status::Fail;
    }
  }
  return result;
}

SelfTest::Status SelfTest::testIMU() {
  Status result = Status::Running;
  if (!imu.accelValid()) {
    selfTestInfo("* SELF TEST *   IMU   * FAIL - IMU not ready");
    result = Status::Fail;
  } else {
    float accelTotal = imu.getAccel();
    // Check if acceleration values are within a reasonable range
    if (accelTotal < 1.2f && accelTotal > 0.8f) {
      result = Status::Pass;
      selfTestInfo("* SELF TEST *   IMU   * PASS - Total acceleration: %g", accelTotal);
    } else {
      selfTestInfo("* SELF TEST *   IMU   * FAIL - Total acceleration out of range: %g",
                   accelTotal);
      result = Status::Fail;
    }
  }
  return result;
}

SelfTest::Status SelfTest::testAmbient() {
  Status result = Status::Running;
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
  Status result = Status::Running;
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
  Status result = Status::Running;
  // Placeholder implementation
  result = Status::Pass;
  return result;
}

///////////////////////////////////////////////
// Button Test

SelfTest::Status ButtonsInteractiveTest::update() {
  if (status != SelfTest::Status::Running) {
    status = SelfTest::Status::Running;
    // reset tracking variables
    upPressed = false;
    downPressed = false;
    leftPressed = false;
    rightPressed = false;
    centerPressed = false;
    waitForInput = 800;  // reset timeout (10ms ticks)
    Serial.println("* SELF TEST * BUTTONS * Starting button test - please press each button");
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
    buttonsTest.status = SelfTest::Status::Fail;
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
    buttonsTest.status = SelfTest::Status::Pass;
    speaker.playSound(fx::confirm);
    selfTestInfo("* SELF TEST * BUTTONS * PASS - All buttons detected");
    // delay(750);  // pause to let user see success on display screen
  }

  // handle test results if test is complete
  if (buttonsTest.status != SelfTest::Status::Running) {
    Serial.println("* SELF TEST * BUTTONS * Test complete");
    display.update();
    delay(500);                    // pause to let user see test results on display screen
    selfTest_pageButtons.close();  // close button test display page
  }

  return status;
}

///////////////////////////////////////////////
// Vario Test

SelfTest::Status VarioInteractiveTest::update() {
  // initialize the test
  if (status != SelfTest::Status::Running) {
    status = SelfTest::Status::Running;
    initializedTest = false;
    waitForInput = 800;         // reset timeout (10ms ticks)
    selfTest_pageVario.show();  // show display page for vario test
  }

  // delay if baro not ready yet
  if (baro.state() != Barometer::State::Ready || baro.climbRateFilteredValid() == false) {
    // If this is our first time delaying, send waiting message
    if (!delayForCalibration)
      selfTestInfo("* SELF TEST *  VARIO  * DELAY - Waiting for Baro Calibration");
    delayForCalibration = true;
    return status;
  }
  if (initializedTest == false) {
    initializedTest = true;
    if (delayForCalibration)
      speaker.playSound(fx::confirm);  // if we delayed, play sound to alert user we're starting now
    delayForCalibration = false;
    initialAltitude = baro.altF();
    maxAltitude = initialAltitude;
    deltaAltitude = 0.0f;
    maxClimb = 0.0f;
    maxSink = 0.0f;
    Serial.println("* SELF TEST *  VARIO  * Starting vario test - please raise & lower quickly");
  }

  // check for sufficient vario values
  float alt = baro.altF();
  if (alt > maxAltitude) {
    maxAltitude = alt;
    deltaAltitude = maxAltitude - initialAltitude;
  }
  climb = baro.climbRate();
  if (climb > maxClimb) {
    maxClimb = climb;
  } else if (climb < maxSink) {
    maxSink = climb;
  }

  // fail test if timeout reached
  if (waitForInput-- <= 0) {
    status = SelfTest::Status::Fail;
    speaker.playSound(fx::cancel);
    selfTestInfo("* SELF TEST *  VARIO  * FAIL - Timeout waiting for climb & sink");
  }

  // if vario values sufficient, pass the test
  if (deltaAltitude >= 0.5f && maxClimb >= 1.0f && maxSink <= -1.0f) {
    status = SelfTest::Status::Pass;
    speaker.playSound(fx::confirm);
    selfTestInfo(
        "* SELF TEST *  VARIO  * PASS - Detected altitude change of %g m, max climb %g m/s, max "
        "sink %g m/s",
        deltaAltitude, maxClimb, maxSink);
  }

  // handle test results (or continue test)
  if (status != SelfTest::Status::Running) {
    Serial.println("* SELF TEST *  VARIO  * Test complete");
    display.update();
    delay(500);                  // pause to let user see test results on display screen
    selfTest_pageVario.close();  // close vario test display page
  }

  return status;
}

///////////////////////////////////////////////
// Power Test

SelfTest::Status SelfTest::testPower() {
  Status result = Status::Running;
  // Placeholder implementation
  result = Status::Pass;
  return result;
}

SelfTest::Status SelfTest::testSpeaker() {
  Status result = Status::Running;

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

////////////////////////////////////////////////
// SelfTest methods to run tests

SelfTest::Status SelfTest::runAllTests() {
  status = Status::Running;
  if (statusAutoTests == Status::Running || statusAutoTests == Status::Unknown) {
    statusAutoTests = runAutoTests(false);  // keep file open
  } else if (statusInteractiveTests == Status::Running ||
             statusInteractiveTests == Status::Unknown) {
    statusInteractiveTests = runInteractiveTests(true);  // close file when done
  } else {
    status = Status::Complete;  // we're done
    selfTestInfo("* SELF TEST * All tests complete");
  }
  return status;
}

SelfTest::Status SelfTest::runAutoTests(bool closeFileWhenDone) {
  statusAutoTests = Status::Running;

  if (selfTest.results.sdCard == Status::Unknown || selfTest.results.sdCard == Status::Running) {
    selfTest.results.sdCard = testSDCard();
  } else if (selfTest.results.baro == Status::Unknown || selfTest.results.baro == Status::Running) {
    selfTest.results.baro = testBaro();
  } else if (selfTest.results.imu == Status::Unknown || selfTest.results.imu == Status::Running) {
    selfTest.results.imu = testIMU();
  } else if (selfTest.results.gps == Status::Unknown || selfTest.results.gps == Status::Running) {
    selfTest.results.gps = testGPS();
  } else if (selfTest.results.ambient == Status::Unknown ||
             selfTest.results.ambient == Status::Running) {
    selfTest.results.ambient = testAmbient();
  } else if (selfTest.results.display == Status::Unknown ||
             selfTest.results.display == Status::Running) {
    selfTest.results.display = testDisplay();
  } else if (selfTest.results.power == Status::Unknown ||
             selfTest.results.power == Status::Running) {
    selfTest.results.power = testPower();
  } else {
    statusAutoTests = Status::Complete;  // auto tests complete
    if (closeFileWhenDone && self_test_file) {
      self_test_file.close();
    }
    selfTestInfo("* SELF TEST * Auto tests complete");
  }
  return statusAutoTests;
}

SelfTest::Status SelfTest::runInteractiveTests(bool closeFileWhenDone) {
  statusInteractiveTests = Status::Running;

  if (buttonsTest.status == SelfTest::Status::Unknown ||
      buttonsTest.status == SelfTest::Status::Running) {
    selfTest.results.buttons = buttonsTest.update();
  } else if (varioTest.status == SelfTest::Status::Unknown ||
             varioTest.status == SelfTest::Status::Running) {
    selfTest.results.vario = varioTest.update();
  }
  // else if (other tests go here)
  else {
    statusInteractiveTests = Status::Complete;  // interactive tests complete
    if (closeFileWhenDone && self_test_file) {
      self_test_file.close();
    }
    selfTestInfo("* SELF TEST * Interactive tests complete");
  }

  return statusInteractiveTests;
}

void SelfTest::clearResults() {
  selfTest.results.reset();
  selfTest.status = Status::Unknown;
  selfTest.statusAutoTests = Status::Unknown;
  selfTest.statusInteractiveTests = Status::Unknown;
  buttonsTest.status = Status::Unknown;
  varioTest.status = Status::Unknown;
}