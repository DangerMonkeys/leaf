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
constexpr uint32_t IMU_TEST_TIMEOUT_MS = 20000;
constexpr uint32_t GPS_FIX_TEST_TIMEOUT_MS = 30UL * 60UL * 1000UL;
File self_test_file;
String self_test_file_name = "";

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

bool waitForVarioStartButton = false;
bool varioStartPromptComplete = false;
SelfTest_PageVarioReady selfTest_pageVarioReady;
SelfTest_PageRunning selfTest_pageRunning;

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
  if (!self_test_file) {
    self_test_file_name = "";
    return false;
  }
  self_test_file_name = fileName;

  // Write the version information to know what generated this fatal error
  self_test_file.print("`hardware_version=");
  self_test_file.print(LeafVersionInfo::hardwareVariant());
  self_test_file.println("`");
  self_test_file.print("`firmware_version=");
  self_test_file.print(LeafVersionInfo::firmwareVersion());
  self_test_file.println("`");
  self_test_file.print("`MACaddress=");
  self_test_file.print(settings.macAddress);
  self_test_file.println("`");

  return self_test_file;
}

bool pressButtonToContinue() {
  if (!waitForVarioStartButton && !varioStartPromptComplete) {
    waitForVarioStartButton = true;
    Serial.println("* SELF TEST *  VARIO  * Waiting for user to start vario test");
    selfTest_pageVarioReady.show();
    display.update();
  }

  if (!waitForVarioStartButton) {
    return false;
  }

  if (buttons.inspectPins() != Button::NONE) {
    waitForVarioStartButton = false;
    varioStartPromptComplete = true;
    Serial.println("* SELF TEST *  VARIO  * User confirmed ready for vario test");
    selfTest_pageVarioReady.close();
    return false;
  }

  return true;
}

String SelfTest::resultsFileName() const { return self_test_file_name; }

void selfTestInfo(const char* msg, ...) {
  char buffer[BUFFER_SIZE];

  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, BUFFER_SIZE, msg, args);
  va_end(args);

  Serial.println(buffer);
  if (useSDFile()) {
    self_test_file.print("* ");
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
    selfTestInfo("`Test=SD_CARD`,    `Result=FAIL`, `Message=Physical card detection failed`");
    result = Status::Fail;
  } else {
    bool formatAttempted = false;

    if (!sdcard.isMounted()) {
      Serial.println("* SELF TEST * SD CARD * Mount failed; attempting SD card format");
      formatAttempted = true;
      sdcard.format();
    }

    if (sdcard.isMounted() && !useSDFile()) {
      Serial.println("* SELF TEST * SD CARD * Cannot save results; attempting SD card format");
      formatAttempted = true;
      sdcard.format();
    }

    if (!sdcard.isMounted()) {
      selfTestInfo("`Test=SD_CARD`,    `Result=FAIL`, `Message=SD Card not mounted%s`",
                   formatAttempted ? " after format attempt" : "");
      result = Status::Fail;
    } else if (!useSDFile()) {
      selfTestInfo("`Test=SD_CARD`,    `Result=FAIL`, `Message=Cannot save self test results%s`",
                   formatAttempted ? " after format attempt" : "");
      result = Status::Fail;
    } else {
      selfTestInfo("`Test=SD_CARD`,    `Result=PASS`, `Message=%sSaving self test results`",
                   formatAttempted ? "Formatted SD card. " : "");
      result = Status::Pass;
    }
  }
  return result;
}

/////////////////////////////////////////////
// BARO TEST
SelfTest::Status SelfTest::testBaro() {
  Status result = Status::Running;
  if (baro.state() != Barometer::State::Ready) {
    selfTestInfo("`Test=BARO`,       `Result=FAIL`, `Message=Barometer not ready`");
    result = Status::Fail;
  } else {
    if (baro.altF() < 1100 && baro.altF() > -200) {
      selfTestInfo("`Test=BARO`,       `Result=PASS`, `Alt_m=%g`", baro.altF());
      result = Status::Pass;
    } else {
      selfTestInfo("`Test=BARO`,       `Result=FAIL`, `Alt_m=%g`, `Message=Value out of range`",
                   baro.altF());
      result = Status::Fail;
    }
  }
  return result;
}
/////////////////////////////////////////////
// IMU TEST
bool imuTestInitialized = false;
uint32_t imuTestStartMillis = 0;
SelfTest::Status SelfTest::testIMU() {
  Status result = Status::Running;
  if (!imuTestInitialized) {
    imuTestInitialized = true;
    imuTestStartMillis = millis();
  }

  if (!imu.accelValid() || !imu.velocityValid()) {
    uint32_t elapsedMillis = millis() - imuTestStartMillis;
    if (elapsedMillis >= IMU_TEST_TIMEOUT_MS) {
      selfTestInfo(
          "`Test=IMU`,        `Result=FAIL`, `Message=IMU timeout waiting for valid accel and "
          "gravity`, `Elapsed_ms=%u`, `AccelValid=%d`, `GravityReady=%d`, "
          "`GravityRemaining=%u`, `Gravity=%g`, `AwzValid=%d`, `Awz=%g`, "
          "`GravityResets=%u`, `LastRejectedGravity=%g`",
          elapsedMillis, imu.accelValid(), imu.velocityValid(), imu.gravityInitSamplesRemaining(),
          imu.gravityEstimate(), imu.worldVerticalAccelValid(), imu.lastWorldVerticalAccel(),
          imu.gravityInitResetCount(), imu.lastRejectedGravityEstimate());
      result = Status::Fail;
      imuTestInitialized = false;
    }
  } else {
    float accelTotal = imu.getAccel();
    // Check if acceleration values are within a reasonable range
    if (accelTotal < 1.2f && accelTotal > 0.8f) {
      result = Status::Pass;
      selfTestInfo("`Test=IMU`,        `Result=PASS`, `Accel=%g`", accelTotal);
    } else {
      selfTestInfo("`Test=IMU`,        `Result=FAIL`, `Accel=%g`, `Message=Out of range`",
                   accelTotal);
      result = Status::Fail;
    }
    imuTestInitialized = false;
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
      selfTestInfo("`Test=AMBIENT`,    `Result=FAIL`, `Message=Ambient sensor not ready`");
      result = Status::Fail;
    }
  } else {
    float temperature = ambient.temp();
    if (temperature < 45.0f && temperature > 5.0f) {
      result = Status::Pass;
      selfTestInfo("`Test=AMBIENT`,    `Result=PASS`, `Temp=%g`", temperature);
    } else {
      selfTestInfo("`Test=AMBIENT`,    `Result=FAIL`, `Temp=%g`, `Message=Out of range`",
                   temperature);
      result = Status::Fail;
    }
  }
  return result;
}

/////////////////////////////////////////////
// GPS SATELLITE TEST
SelfTest::Status SelfTest::testGPSsats() {
  Status result = Status::Running;
  if (gps.fixInfo.numberOfSats) {  // Pass if we see >0 satellites...
    result = Status::Pass;
    selfTestInfo("`Test=GPS_SATS`,   `Result=PASS`, `Sats=%d`", gps.fixInfo.numberOfSats);
  } else {
    result = Status::Fail;
    selfTestInfo("`Test=GPS_SATS`,   `Result=FAIL`, `Sats=%d`, `Message=No satellites detected`",
                 gps.fixInfo.numberOfSats);
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
  }

  if (gps.passedChecksum() > gpsSerialInitialPassedChecksumCount) {
    gpsSerialTestInitialized = false;
    selfTestInfo("`Test=GPS_SERIAL`, `Result=PASS`, `Message=Valid NMEA sentence received`");
    return Status::Pass;
  }

  if (millis() - gpsSerialStartMillis >= GPS_SERIAL_TEST_TIMEOUT_MS) {
    gpsSerialTestInitialized = false;
    selfTestInfo("`Test=GPS_SERIAL`, `Result=FAIL`, `Message=No valid NMEA sentence received`");
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
    selfTest_pageGPSFix.show();
    display.update();
  }

  if (gpsFixTestCancelled) {
    gpsFixTestInitialized = false;
    selfTestInfo("`Test=GPS_FIX`,    `Result=FAIL`, `Message=Cancelled by user`");
    return Status::Fail;
  }

  if (gps.sentencesWithFix() > gpsFixInitialSentencesWithFixCount) {
    gpsFixTestInitialized = false;
    uint32_t timeToFixSeconds = GPS_FIX_TEST_TIMEOUT_MS / 1000 - gpsFixRemainingSeconds;
    selfTest_pageGPSFix.close();
    selfTestInfo("`Test=GPS_FIX`,    `Result=PASS`, `Sec_to_fix=%d`, `Fix_sats=%d`",
                 timeToFixSeconds, gps.fixInfo.numberOfSats);
    return Status::Pass;
  }

  const uint32_t elapsedMillis = millisNow - gpsFixStartMillis;
  if (elapsedMillis >= GPS_FIX_TEST_TIMEOUT_MS) {
    gpsFixTestInitialized = false;
    gpsFixRemainingSeconds = 0;
    display.update();
    selfTest_pageGPSFix.close();
    selfTestInfo("`Test=GPS_FIX`,    `Result=FAIL`, `Message=Timeout waiting for GPS fix`");
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

  bool battCharging = power.info().charging;
  uint8_t battPercent = power.info().batteryPercent;
  String messageStr = "";      // store additional message details
  String extra326Params = "";  // extra  power info on hardware v3.2.6+

#ifdef LED_PIN  // (only for v3.2.6+ with controllable LED and PowerGood input)
  bool usbInput = power.info().USBinput;

  // Pass if: [ USB Power AND (charging OR full) ] OR [ NOT USB Power AND NOT charging ]
  if ((usbInput && (battPercent >= 100 || battCharging)) || (!usbInput && !battCharging)) {
    result = Status::Pass;
  } else {
    result = Status::Fail;
    messageStr = "Inconsistent power state";
  }
  extra326Params = ", `USB_input=" + String(usbInput) + "`";
#else  // (for previous versions (<= v3.2.5) without PowerGood input)
  // Pass if:  [charging] OR [ not-charging AND full ]
  if (battCharging || (!battCharging && battPercent >= 100)) {
    result = Status::Pass;
  } else {
    result = Status::Fail;
    messageStr = "Inconsistent power state";
  }
#endif

  if (power.info().batteryMV < 3200 || power.info().batteryMV > 4250) {
    if (result == Status::Fail) {
      // if we already failed above due to inconsistent state, append to message
      messageStr += ". ";
    }
    result = Status::Fail;
    messageStr += "Battery voltage out of range";
  }

  String messageField = messageStr == "" ? "" : ", `Message=" + messageStr + "`";

  selfTestInfo("`Test=POWER`,      `Result=%s`, `Batt%%=%d%%`, `Batt_mV=%d`, `Batt_charge=%d`%s%s",
               result == Status::Pass ? "PASS" : "FAIL", battPercent, power.info().batteryMV,
               battCharging, extra326Params.c_str(), messageField.c_str());

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
    selfTestInfo(
        "`Test=BUTTONS`,    `Result=FAIL`, `Up=%d`, `Down=%d`, `Left=%d`, `Right=%d`, `Center=%d`, "
        "`Message=Timeout waiting for all button presses`",
        upPressed, downPressed, leftPressed, rightPressed, centerPressed);
  }

  // Test passes if all buttons have been pressed
  if (upPressed && downPressed && leftPressed && rightPressed && centerPressed &&
      buttonsTest.status == SelfTest::Status::Running) {
    buttonsTest.status = SelfTest::Status::Pass;
    speaker.playSound(fx::confirm);
    selfTestInfo(
        "`Test=BUTTONS`,    `Result=PASS`, `Up=%d`, `Down=%d`, `Left=%d`, `Right=%d`, `Center=%d`",
        upPressed, downPressed, leftPressed, rightPressed, centerPressed);
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
    // Play sound to get user's attention for vario test instructions
    speaker.playSound(fx::confirm);
  }

  // delay if baro not ready yet
  if (baro.state() != Barometer::State::Ready || baro.climbRateFilteredValid() == false) {
    // If this is our first time delaying, send waiting message
    if (!delayForCalibration)
      Serial.println("* SELF TEST *  VARIO  * DELAY - Waiting for Baro Calibration");
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
  climb = baro.climbRateFiltered() / 100.0f;  // convert from cm/s to m/s
  if (climb > maxClimb) {
    maxClimb = climb;
  } else if (climb < maxSink) {
    maxSink = climb;
  }

  // fail test if timeout reached
  if (waitForInput-- <= 0) {
    status = SelfTest::Status::Fail;
    speaker.playSound(fx::cancel);
    selfTestInfo(
        "`Test=VARIO`,      `Result=FAIL`, `DeltaAlt_m=%.2f`, `MaxClimb_ms=%.2f`, "
        "`MaxSink_ms=%.2f`",
        deltaAltitude, maxClimb, maxSink);
  }

  // if vario values sufficient, pass the test
  if (deltaAltitude >= 0.4f && maxClimb >= 1.0f && maxSink <= -1.0) {
    status = SelfTest::Status::Pass;
    speaker.playSound(fx::confirm);
    selfTestInfo(
        "`Test=VARIO`,      `Result=PASS`, `DeltaAlt_m=%.2f`, `MaxClimb_ms=%.2f`, "
        "`MaxSink_ms=%.2f`",
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

  // wait ~1.5 seconds before starting the test to give user time to read instructions
  if (speakerTestCounter < 150) {
    return status;
  }

  // Play Test Sounds for user to confirm volume levels function properly
  if (speakerTestCounter == 150) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Low);
    speaker.playSound(fx::neutralLong);
  } else if (speakerTestCounter == 250) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Medium);
    speaker.playSound(fx::neutralLong);
  } else if (speakerTestCounter == 350) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::High);
    speaker.playSound(fx::neutralLong);
  }

  if (speakerTestCounter == 450) {
    speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Off);
    speaker.playSound(fx::quadRise);
    Serial.println(
        "* SELF TEST * SPEAKER * Hear long beeps at low, med, high volume AND NOT 4-beeps?");
    Serial.println("* SELF TEST * SPEAKER * YES = UP button, NO = DOWN button");
  }

  if (speakerTestCounter > 450 && status == SelfTest::Status::Running) {
    // check for user input
    Button button = Button::NONE;
    button = buttons.inspectPins();
    if (button == Button::UP) {
      status = SelfTest::Status::Pass;
      selfTestInfo("`Test=SPEAKER`,    `Result=PASS`, `Message=PASS - via user input`");
    } else if (button == Button::DOWN) {
      status = SelfTest::Status::Fail;
      selfTestInfo("`Test=SPEAKER`,    `Result=FAIL`, `Message=FAIL - via user input`");
    }
  }

  if (speakerTestCounter >= 450 + 800) {
    status = SelfTest::Status::Fail;
    selfTestInfo(
        "`Test=SPEAKER`,    `Result=FAIL`, `Message=FAIL - Timeout waiting for user input`");
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
    selfTest_pageRunning.show();
    display.update();
  }
}

bool SelfTest::tallyResults() {
  bool allPass = true;
  if (results.sdCard != Status::Pass || results.baro != Status::Pass ||
      results.imu != Status::Pass || results.gpsSerial != Status::Pass ||
      results.gpsFix != Status::Pass || results.ambient != Status::Pass ||
      results.display != Status::Pass || results.buttons != Status::Pass ||
      results.power != Status::Pass || results.speaker != Status::Pass ||
      results.vario != Status::Pass) {
    allPass = false;
  }
  return allPass;
}

SelfTest_PageResults selfTest_pageResults;
SelfTest_PageCommissioningConfirmation selfTest_pageCommissioningConfirmation;

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
      selfTest.results.allTests = tallyResults() ? Status::Pass : Status::Fail;
      if (selfTest.results.allTests == Status::Pass) {
        selfTestInfo("`Test=ALL`,        `Result=PASS`, `Message=ALL TESTS PASSED`");
        speaker.playSound(fx::confirm);
      } else {
        selfTestInfo("`Test=ALL`,        `Result=FAIL`, `Message=SOME TESTS FAILED`");
        speaker.playSound(fx::fatalerror);
      }
      closeTestFile();
      selfTest_pageRunning.close();
      if (selfTest.results.allTests == Status::Pass) {
        selfTest_pageCommissioningConfirmation.show();
      } else {
        selfTest_pageResults.show();  // show results page when any test fails
      }
      display.update();
    }
  } else {
    updateNeeded = false;  // not running, no need to call update again
  }
  return updateNeeded;
}

bool SelfTest::updateNeeded() { return status == Status::Running; }

void SelfTest::confirmCommissioningComplete() {
  commissioning_complete_confirmed = true;
  display.update();
}

bool SelfTest::commissioningCompleteConfirmed() const { return commissioning_complete_confirmed; }

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
  } else if (selfTest.results.gpsSerial == Status::Unknown ||
             selfTest.results.gpsSerial == Status::Running) {
    selfTest.results.gpsSerial = testGPSserial();
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
      closeTestFile();
    }
  }
  return statusAutoTests;
}

SelfTest::Status SelfTest::runInteractiveTests(bool closeFileWhenDone) {
  statusInteractiveTests = Status::Running;

  if (!varioStartPromptComplete && pressButtonToContinue()) {
    return statusInteractiveTests;
  } else if (selfTest.results.vario == SelfTest::Status::Unknown ||
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
    statusInteractiveTests = Status::Complete;  // interactive tests complete
    if (closeFileWhenDone && self_test_file) {
      closeTestFile();
    }
  }

  return statusInteractiveTests;
}

void SelfTest::clearResults() {
  if (self_test_file) {
    self_test_file.close();
  }
  self_test_file_name = "";
  commissioning_complete_confirmed = false;
  selfTest.results.reset();
  selfTest.status = Status::Unknown;
  selfTest.statusAutoTests = Status::Unknown;
  selfTest.statusInteractiveTests = Status::Unknown;
  ambientTestCounter = 0;
  imuTestInitialized = false;

  // reset interactive tests
  buttonsTest.status = Status::Unknown;
  varioTest.status = Status::Unknown;
  speakerTest.status = Status::Unknown;
  gpsSerialTestInitialized = false;
  gpsFixTestInitialized = false;
  gpsFixTestCancelled = false;
  waitForVarioStartButton = false;
  varioStartPromptComplete = false;
}
