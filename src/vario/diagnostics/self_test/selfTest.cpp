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
#include "ui/settings/settings.h"

SelfTest selfTest;
ButtonsInteractiveTest buttonsTest;
VarioInteractiveTest varioTest;
SpeakerInteractiveTest speakerTest;

constexpr size_t BUFFER_SIZE = 512;
constexpr uint32_t GPS_SERIAL_TEST_TIMEOUT_MS = 5000;
constexpr uint32_t GPS_FIX_TEST_TIMEOUT_MS = 30UL * 60UL * 1000UL;
File self_test_file;

bool gpsSerialTestInitialized = false;
uint32_t gpsSerialInitialPassedChecksumCount = 0;
uint32_t gpsSerialStartMillis = 0;

bool gpsFixTestInitialized = false;
uint32_t gpsFixInitialSentencesWithFixCount = 0;
uint32_t gpsFixStartMillis = 0;
uint32_t gpsFixRemainingSeconds = GPS_FIX_TEST_TIMEOUT_MS / 1000;
uint32_t gpsFixLastDisplayUpdateMillis = 0;
bool gpsFixTestCancelled = false;
SelfTest_PageGPSFix selfTest_pageGPSFix{&gpsFixRemainingSeconds, &gpsFixTestCancelled};

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
  self_test_file.print("MAC Address: ");
  self_test_file.println(settings.macAddress);

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

/////////////////////////////////////////////
// SD CARD TEST
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

/////////////////////////////////////////////
// BARO TEST
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
/////////////////////////////////////////////
// IMU TEST
uint16_t imuTestCounter = 0;
SelfTest::Status SelfTest::testIMU() {
  Status result = Status::Running;
  if (!imu.accelValid()) {
    if (imuTestCounter++ >= 400) {
      selfTestInfo("* SELF TEST *   IMU   * FAIL - IMU acceleration not valid");
      result = Status::Fail;
    }
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

/////////////////////////////////////////////
// AMBIENT TEST
uint16_t ambientTestCounter = 0;
SelfTest::Status SelfTest::testAmbient() {
  Status result = Status::Running;
  if (ambient.state() != Ambient::State::Ready) {
    if (ambientTestCounter++ >= 400) {
      selfTestInfo("* SELF TEST * AMBIENT * FAIL - Ambient sensor not ready");
      result = Status::Fail;
    }
  } else {
    float temperature = ambient.temp();
    if (temperature < 45.0f && temperature > 0.0f) {
      result = Status::Pass;
      selfTestInfo("* SELF TEST * AMBIENT * PASS");
    } else {
      selfTestInfo("* SELF TEST * AMBIENT * Ambient temperature out of range: %g", temperature);
    }
  }
  return result;
}

/////////////////////////////////////////////
// GPS TEST
SelfTest::Status SelfTest::testGPS() {
  Status result = Status::Running;
  if (!gps.fixInfo.numberOfSats) {  // Pass if we see >0 satellites...
    result = testGPSserial();  // ...otherwise proceed to check serial communication with module
  } else {
    result = Status::Pass;
    selfTestInfo("* SELF TEST *   GPS   * PASS - GPS sees %d satellites", gps.fixInfo.numberOfSats);
  }
  return result;
}

/////////////////////////////////////////////
// GPS SERIAL TEST
SelfTest::Status SelfTest::testGPSserial() {
  if (!gpsSerialTestInitialized) {
    gpsSerialTestInitialized = true;
    gpsSerialInitialPassedChecksumCount = gps.passedChecksum();
    gpsSerialStartMillis = millis();
    selfTestInfo(
        "* SELF TEST * GPS SER * INFO - GPS sees zero satellites; waiting for valid NMEA "
        "sentence");
  }

  if (gps.passedChecksum() > gpsSerialInitialPassedChecksumCount) {
    gpsSerialTestInitialized = false;
    selfTestInfo("* SELF TEST * GPS SER * PASS - Valid NMEA sentence received");
    return Status::Pass;
  }

  if (millis() - gpsSerialStartMillis >= GPS_SERIAL_TEST_TIMEOUT_MS) {
    gpsSerialTestInitialized = false;
    selfTestInfo("* SELF TEST * GPS SER * FAIL - No valid NMEA sentence received");
    return Status::Fail;
  }

  return Status::Running;
}

/////////////////////////////////////////////
// GPS FIX TEST
SelfTest::Status SelfTest::testGPSfix() {
  const uint32_t millisNow = millis();

  if (!gpsFixTestInitialized) {
    gpsFixTestInitialized = true;
    gpsFixTestCancelled = false;
    gpsFixInitialSentencesWithFixCount = gps.sentencesWithFix();
    gpsFixStartMillis = millisNow;
    gpsFixLastDisplayUpdateMillis = 0;
    gpsFixRemainingSeconds = GPS_FIX_TEST_TIMEOUT_MS / 1000;
    selfTestInfo("* SELF TEST * GPS FIX * INFO - Waiting for GPS fix");
    selfTest_pageGPSFix.show();
    display.update();
  }

  if (gpsFixTestCancelled) {
    gpsFixTestInitialized = false;
    selfTestInfo("* SELF TEST * GPS FIX * FAIL - Cancelled by user");
    return Status::Fail;
  }

  if (gps.sentencesWithFix() > gpsFixInitialSentencesWithFixCount) {
    gpsFixTestInitialized = false;
    selfTest_pageGPSFix.close();
    selfTestInfo("* SELF TEST * GPS FIX * PASS - GPS fix acquired with %d satellites",
                 gps.fixInfo.numberOfSats);
    return Status::Pass;
  }

  const uint32_t elapsedMillis = millisNow - gpsFixStartMillis;
  if (elapsedMillis >= GPS_FIX_TEST_TIMEOUT_MS) {
    gpsFixTestInitialized = false;
    gpsFixRemainingSeconds = 0;
    display.update();
    selfTest_pageGPSFix.close();
    selfTestInfo("* SELF TEST * GPS FIX * FAIL - Timeout waiting for GPS fix");
    return Status::Fail;
  }

  gpsFixRemainingSeconds = (GPS_FIX_TEST_TIMEOUT_MS - elapsedMillis + 999) / 1000;
  if (millisNow - gpsFixLastDisplayUpdateMillis >= 1000) {
    gpsFixLastDisplayUpdateMillis = millisNow;
    display.update();
  }

  return Status::Running;
}

/////////////////////////////////////////////
// DISPLAY TEST
SelfTest::Status SelfTest::testDisplay() {
  Status result = Status::Running;
  // Placeholder implementation
  result = Status::Pass;
  return result;
}

/////////////////////////////////////////////
// POWER TEST
SelfTest::Status SelfTest::testPower() {
  Status result = Status::Running;

  power.info().charging ? selfTestInfo("* SELF TEST *  POWER  * Battery charging")
                        : selfTestInfo("* SELF TEST *  POWER  * Battery NOT charging");
  selfTestInfo("* SELF TEST *  POWER  * Battery percent: %d%%", power.info().batteryPercent);

#ifdef LED_PIN  // (only for v3.2.6+ with controllable LED and PowerGood input)
  power.info().USBinput ? selfTestInfo("* SELF TEST *  POWER  * USB Power detected")
                        : selfTestInfo("* SELF TEST *  POWER  * USB Power NOT detected");
  // Pass if: [ USB Power AND (charging OR full) ] OR [ NOT USB Power AND NOT charging ]
  if ((power.info().USBinput && (power.info().batteryPercent >= 100 || power.info().charging)) ||
      (!power.info().USBinput && !power.info().charging)) {
    result = Status::Pass;
  } else {
    result = Status::Fail;
  }
#else  // (for previous versions (<= v3.2.5) without PowerGood input)
  // Pass if:  [charging] OR [ not-charging AND full ]
  if (power.info().charging || (!power.info().charging && power.info().batteryPercent >= 100)) {
    result = Status::Pass;
  } else {
    result = Status::Fail;
  }
#endif

  if (power.info().batteryMV < 3200 || power.info().batteryMV > 4250) {
    selfTestInfo("* SELF TEST *  POWER  * FAIL - Battery outside voltage range: %d mV",
                 power.info().batteryMV);
    result = Status::Fail;
  }

  if (result == Status::Pass) {
    selfTestInfo("* SELF TEST *  POWER  * PASS");
  } else {
    selfTestInfo("* SELF TEST *  POWER  * FAIL - Inconsistent power state");
  }

  return result;
}

///////////////////////////////////////////////
// Button Test (Interactive)
SelfTest::Status ButtonsInteractiveTest::update() {
  if (status != SelfTest::Status::Running && !waitingForSpeaker) {
    status = SelfTest::Status::Running;
    // reset tracking variables
    upPressed = false;
    downPressed = false;
    leftPressed = false;
    rightPressed = false;
    centerPressed = false;
    waitingForSpeaker = false;
    waitForInput = 800;  // reset timeout (10ms ticks)
    Serial.println("* SELF TEST * BUTTONS * Starting button test - please press each button");
    selfTest_pageButtons.show();  // show display page for button test
    display.update();
    // turn off vario volume
    speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)0);
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
  if (buttonsTest.waitForInput-- == 0) {
    buttonsTest.status = SelfTest::Status::Fail;
    speaker.playSound(fx::cancel);
    selfTestInfo("* SELF TEST * BUTTONS * FAIL - Timeout waiting for button presses");
    if (!upPressed) {
      selfTestInfo("* SELF TEST * BUTTONS * FAIL - UP button NOT DETECTED");
    }
    if (!downPressed) {
      selfTestInfo("* SELF TEST * BUTTONS * FAIL - DOWN button NOT DETECTED");
    }
    if (!leftPressed) {
      selfTestInfo("* SELF TEST * BUTTONS * FAIL - LEFT button NOT DETECTED");
    }
    if (!rightPressed) {
      selfTestInfo("* SELF TEST * BUTTONS * FAIL - RIGHT button NOT DETECTED");
    }
    if (!centerPressed) {
      selfTestInfo("* SELF TEST * BUTTONS * FAIL - CENTER button NOT DETECTED");
    }
  }

  // Test passes if all buttons have been pressed
  if (upPressed && downPressed && leftPressed && rightPressed && centerPressed &&
      buttonsTest.status == SelfTest::Status::Running) {
    buttonsTest.status = SelfTest::Status::Pass;
    speaker.playSound(fx::confirm);
    selfTestInfo("* SELF TEST * BUTTONS * PASS - All buttons detected");
  }

  // close if test is complete
  if (buttonsTest.status != SelfTest::Status::Running) {
    if (speaker.update()) {
      waitingForSpeaker = true;
      return SelfTest::Status::Running;  // delay to let sound finish playing
    } else {
      waitingForSpeaker = false;
      selfTest_pageButtons.close();  // close button test display page
    }
  }

  return status;
}

///////////////////////////////////////////////
// Vario Test (Interactive)
SelfTest::Status VarioInteractiveTest::update() {
  // initialize the test
  if (status != SelfTest::Status::Running) {
    status = SelfTest::Status::Running;
    initializedTest = false;
    waitForInput = 800;         // reset timeout (10ms ticks)
    selfTest_pageVario.show();  // show display page for vario test
    display.update();
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
  climb = baro.climbRateFiltered();
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
  if (deltaAltitude >= 0.4f && maxClimb >= 150.0f && maxSink <= -150.0f) {
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
    selfTest_pageVario.close();  // close vario test display page
  }

  return status;
}

///////////////////////////////////////////////
// Speaker Test (Interactive)
SelfTest::Status SpeakerInteractiveTest::update() {
  // initialize the test
  if (status != SelfTest::Status::Running) {
    status = SelfTest::Status::Running;
    speakerTestCounter = 0;
    selfTest_pageSpeaker.show();  // show display page for speaker test
    display.update();
    // turn off vario volume to avoid interference with speaker test tones
    speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)0);
  }

  speakerTestCounter++;

  // wait ~3 seconds before starting the test to give user time to read instructions
  if (speakerTestCounter < 300) {
    return status;
  }

  // Play Test Sounds for user to confirm volume levels function properly
  if (speakerTestCounter == 200) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Low);
    speaker.playSound(fx::neutralLong);
  } else if (speakerTestCounter == 30) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Medium);
    speaker.playSound(fx::neutralLong);
  } else if (speakerTestCounter == 400) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::High);
    speaker.playSound(fx::neutralLong);
  }

  if (speakerTestCounter == 500) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Off);
    speaker.playSound(fx::quadRise);
    Serial.println(
        "* SELF TEST * SPEAKER * Hear long beeps at low, med, high volume AND NOT 4-beeps?");
    Serial.println("* SELF TEST * SPEAKER * YES = UP button, NO = DOWN button");
  }

  if (speakerTestCounter > 750 && status == SelfTest::Status::Running) {
    // check for user input
    Button button = Button::NONE;
    button = buttons.inspectPins();
    if (button == Button::UP) {
      status = SelfTest::Status::Pass;
      selfTestInfo("* SELF TEST * SPEAKER * PASS - via user input");
    } else if (button == Button::DOWN) {
      status = SelfTest::Status::Fail;
      selfTestInfo("* SELF TEST * SPEAKER * FAIL - via user input");
    }
  }

  if (speakerTestCounter >= 750 + 800) {
    status = SelfTest::Status::Fail;
    selfTestInfo("* SELF TEST * SPEAKER * FAIL - Timeout waiting for user input");
  }

  if (status != SelfTest::Status::Running) {
    Serial.println("* SELF TEST * SPEAKER * Test complete");

    // return volume to user setting and cancel any sounds playing
    speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)settings.vario_volume);
    speaker.setVolume(Speaker::SoundChannel::FX, (SpeakerVolume)settings.system_volume);
    speaker.playSound(fx::silence);

    selfTest_pageSpeaker.close();  // close speaker test display page
  }
  return status;
}

////////////////////////////////////////////////
// SelfTest methods to run tests

void SelfTest::begin(bool markAsProductionChecked) {
  if (markAsProductionChecked) {
    if (settings.productionTest) {
      // do nothing, we've already checked the production test
      return;
    } else {
      settings.productionTest = true;  // mark that we've done the production test
      settings.save();
    }
  }

  if (status != Status::Running) {
    display.dismissWarning();  // dismiss liability warning to not block self test screens

    // set status to running so update() will perform tests
    selfTest.clearResults();  // clear any previous results
    speaker.playSound(fx::confirm);
    selfTest.status = SelfTest::Status::Running;
  }
}

bool SelfTest::tallyResults() {
  bool allPass = true;
  if (results.sdCard != Status::Pass || results.baro != Status::Pass ||
      results.imu != Status::Pass || results.gps != Status::Pass ||
      results.gpsFix != Status::Pass || results.ambient != Status::Pass ||
      results.display != Status::Pass || results.buttons != Status::Pass ||
      results.power != Status::Pass || results.speaker != Status::Pass ||
      results.vario != Status::Pass) {
    allPass = false;
  }
  return allPass;
}

SelfTest_PageResults selfTest_pageResults;

bool SelfTest::update() {
  bool updateNeeded = true;  // assume we'll need to call this again
  if (status == Status::Running) {
    if (statusAutoTests == Status::Running || statusAutoTests == Status::Unknown) {
      statusAutoTests = runAutoTests(false);  // false = keep file open
    } else if (statusInteractiveTests == Status::Running ||
               statusInteractiveTests == Status::Unknown) {
      statusInteractiveTests = runInteractiveTests(false);  // false = keep file open

      // all tests complete, tally results
    } else if (status != Status::Complete) {
      status = Status::Complete;  // we're done
      updateNeeded = false;       // no need to call update again since we're complete
      selfTestInfo("* SELF TEST * All tests complete");
      selfTest.results.allTests = tallyResults() ? Status::Pass : Status::Fail;
      if (selfTest.results.allTests == Status::Pass) {
        selfTestInfo("* SELF TEST * ALL TESTS PASSED");
        speaker.playSound(fx::confirm);
      } else {
        selfTestInfo("* SELF TEST * SOME TESTS FAILED");
        speaker.playSound(fx::fatalerror);
      }
      closeTestFile();
      selfTest_pageResults.show();  // show results page when all tests complete
      display.update();
    }
  }
  return updateNeeded;
}

bool SelfTest::updateNeeded() { return status == Status::Running; }

void SelfTest::closeTestFile() {
  if (self_test_file) {
    self_test_file.close();
  }
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
    selfTestInfo("* SELF TEST * Auto tests complete");
    if (closeFileWhenDone && self_test_file) {
      closeTestFile();
    }
  }
  return statusAutoTests;
}

SelfTest::Status SelfTest::runInteractiveTests(bool closeFileWhenDone) {
  statusInteractiveTests = Status::Running;

  if (selfTest.results.vario == SelfTest::Status::Unknown ||
      selfTest.results.vario == SelfTest::Status::Running) {
    selfTest.results.vario = varioTest.update();
  } else if (selfTest.results.buttons == SelfTest::Status::Unknown ||
             selfTest.results.buttons == SelfTest::Status::Running) {
    selfTest.results.buttons = buttonsTest.update();
  } else if (selfTest.results.speaker == SelfTest::Status::Unknown ||
             selfTest.results.speaker == SelfTest::Status::Running) {
    selfTest.results.speaker = speakerTest.update();
  } else if (selfTest.results.gpsFix == SelfTest::Status::Unknown ||
             selfTest.results.gpsFix == SelfTest::Status::Running) {
    selfTest.results.gpsFix = testGPSfix();
  }
  // else if (other tests requiring multiple frames go here)
  else {
    selfTestInfo("* SELF TEST * Interactive tests complete");
    statusInteractiveTests = Status::Complete;  // interactive tests complete
    if (closeFileWhenDone && self_test_file) {
      closeTestFile();
    }
  }

  return statusInteractiveTests;
}

void SelfTest::clearResults() {
  selfTest.results.reset();
  selfTest.status = Status::Unknown;
  selfTest.statusAutoTests = Status::Unknown;
  selfTest.statusInteractiveTests = Status::Unknown;
  ambientTestCounter = 0;
  imuTestCounter = 0;

  // reset interactive tests
  buttonsTest.status = Status::Unknown;
  varioTest.status = Status::Unknown;
  speakerTest.status = Status::Unknown;
  gpsSerialTestInitialized = false;
  gpsFixTestInitialized = false;
  gpsFixTestCancelled = false;
}
