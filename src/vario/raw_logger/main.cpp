#include <Arduino.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <cstring>

#include "hardware/Leaf_SPI.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "raw_logger/flight_tape.h"
#include "raw_logger/recorder_sensors.h"
#include "system/version_info.h"

namespace {
  using namespace leaf::raw;

  constexpr uint8_t POWER_LATCH_PIN = 48;
  constexpr uint8_t LCD_BACKLIGHT_PIN = 21;
  constexpr uint8_t LCD_DC_PIN = 17;
  constexpr uint8_t LCD_RESET_PIN = 18;
  constexpr uint8_t SD_CLK_PIN = 36;
  constexpr uint8_t SD_CMD_PIN = 35;
  constexpr uint8_t SD_D0_PIN = 37;
  constexpr uint8_t SD_D1_PIN = 38;
  constexpr uint8_t SD_D2_PIN = 33;
  constexpr uint8_t SD_D3_PIN = 34;

  constexpr uint16_t IMU_RATE_HZ = 18;
  constexpr uint16_t BARO_RATE_HZ = 100;
  constexpr uint32_t POWER_OFF_HOLD_MS = 3500;

  U8G2_ST75256_JLX19296_F_4W_HW_SPI statusDisplay(U8G2_R1, SPI_SS_LCD, LCD_DC_PIN, LCD_RESET_PIN);

  enum class RecorderState : uint8_t { Starting, WaitingForGps, Ready, Recording, Stopped, Error };
  RecorderState state = RecorderState::Starting;
  const char* errorMessage = "";
  bool sdMounted = false;
  uint32_t lastDisplayMs = 0;
  uint32_t lastMountAttemptMs = 0;
  bool rawButtonState = false;
  bool stableButtonState = false;
  uint32_t buttonChangedMs = 0;
  uint32_t buttonPressedMs = 0;
  bool buttonHoldHandled = false;
  char lastPath[96] = {};

  enum class ButtonAction : uint8_t { None, Click, PowerOff };

  bool mountSdCard() {
    if (sdMounted) return true;
    if (ioexDigitalRead(SD_DETECT_IOEX, SD_DETECT)) return false;
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
    sdMounted = SD_MMC.begin("/sdcard", false, false);
    if (sdMounted && !SD_MMC.exists("/raw")) sdMounted = SD_MMC.mkdir("/raw");
    return sdMounted;
  }

  void initializeBoard() {
    pinMode(POWER_LATCH_PIN, OUTPUT);
    digitalWrite(POWER_LATCH_PIN, HIGH);
    pinMode(BUTTON_PIN_CENTER, INPUT_PULLDOWN);
    rawButtonState = digitalRead(BUTTON_PIN_CENTER) == HIGH;
    stableButtonState = rawButtonState;
    buttonHoldHandled = rawButtonState;  // Ignore the press which physically powered the unit on.
    buttonChangedMs = millis();

    Wire.begin();
    Wire.setClock(400000);
    ioexInit();
    ioexDigitalWrite(GPS_BACKUP_EN_IOEX, GPS_BACKUP_EN, HIGH);
    ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, LOW);
    delay(100);
    ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, HIGH);

    SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, SPI_SS_LCD);
    statusDisplay.setBusClock(20000000);
    statusDisplay.begin();
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
    statusDisplay.setContrast(125);
  }

  const char* stateName() {
    switch (state) {
      case RecorderState::Starting:
        return "STARTING";
      case RecorderState::WaitingForGps:
        return "WAITING GPS";
      case RecorderState::Ready:
        return "READY";
      case RecorderState::Recording:
        return "RECORDING";
      case RecorderState::Stopped:
        return "SAVED";
      case RecorderState::Error:
        return "ERROR";
    }
    return "UNKNOWN";
  }

  void drawFittedText(uint8_t y, char* text) {
    const uint16_t availableWidth = statusDisplay.getDisplayWidth() - 1;
    size_t length = strlen(text);
    while (length && statusDisplay.getStrWidth(text) > availableWidth) {
      text[--length] = '\0';
    }
    statusDisplay.drawStr(0, y, text);
  }

  void drawCenteredText(uint8_t y, const char* text) {
    const uint16_t textWidth = statusDisplay.getStrWidth(text);
    const uint16_t displayWidth = statusDisplay.getDisplayWidth();
    statusDisplay.drawStr(textWidth < displayWidth ? (displayWidth - textWidth) / 2 : 0, y, text);
  }

  void formatDataAmount(char* text, size_t capacity, uint64_t bytes) {
    constexpr uint64_t KIB = 1024;
    constexpr uint64_t MIB = KIB * 1024;
    constexpr uint64_t GIB = MIB * 1024;
    if (bytes < MIB) {
      snprintf(text, capacity, "DATA %llu KB", bytes / KIB);
    } else if (bytes < GIB) {
      snprintf(text, capacity, "DATA %llu.%llu MB", bytes / MIB, (bytes % MIB) * 10 / MIB);
    } else {
      snprintf(text, capacity, "DATA %llu.%llu GB", bytes / GIB, (bytes % GIB) * 10 / GIB);
    }
  }

  void drawStatus() {
    const Statistics stats = flightTape.statistics();
    statusDisplay.clearBuffer();
    statusDisplay.setFont(u8g2_font_6x12_tf);
    drawCenteredText(12, "FLIGHT TAPE");
    statusDisplay.drawHLine(0, 16, statusDisplay.getDisplayWidth());
    statusDisplay.setFont(u8g2_font_7x14B_tf);
    drawCenteredText(36, stateName());
    statusDisplay.setFont(u8g2_font_5x8_tf);

    char line[64];
    snprintf(line, sizeof(line), "GPS %s", recorderSensors.gpsLocked() ? "LOCK" : "WAIT");
    drawFittedText(51, line);
    snprintf(line, sizeof(line), "SAT %u", recorderSensors.satellites());
    drawFittedText(62, line);
    snprintf(line, sizeof(line), "SD %s", sdMounted ? "OK" : "MISSING");
    drawFittedText(73, line);

    snprintf(line, sizeof(line), "BARO %s", recorderSensors.barometerReady() ? "OK" : "NO");
    drawFittedText(84, line);
    snprintf(line, sizeof(line), "IMU %s", recorderSensors.imuReady() ? "OK" : "NO");
    drawFittedText(95, line);
    snprintf(line, sizeof(line), "IMU ERR %lu",
             static_cast<unsigned long>(recorderSensors.imuErrors()));
    drawFittedText(106, line);

    if (state == RecorderState::Recording || state == RecorderState::Stopped) {
      formatDataAmount(line, sizeof(line), stats.bytesWritten);
      drawFittedText(122, line);
      snprintf(line, sizeof(line), "BUF %lu / %u KB",
               static_cast<unsigned long>(stats.bufferHighWaterBytes / 1024), BUFFER_BYTES / 1024);
      drawFittedText(133, line);
      snprintf(line, sizeof(line), "SD %luMS", static_cast<unsigned long>(stats.maxWriteLatencyMs));
      drawFittedText(144, line);
      snprintf(line, sizeof(line), "DROPS %lu", static_cast<unsigned long>(stats.droppedRecords));
      drawFittedText(155, line);
    } else if (state == RecorderState::Error) {
      snprintf(line, sizeof(line), "%s", errorMessage);
      drawFittedText(122, line);
    }

    const char* action = "";
    const char* holdAction = "HOLD: POWER OFF";
    if (stableButtonState && !buttonHoldHandled && millis() - buttonPressedMs >= 800) {
      action = "KEEP HOLDING";
      holdAction = "TO POWER OFF";
    } else if (state == RecorderState::Ready || state == RecorderState::Stopped) {
      action = "CLICK: START";
    } else if (state == RecorderState::Recording) {
      action = "CLICK: STOP";
    }
    statusDisplay.setFont(u8g2_font_6x12_tf);
    snprintf(line, sizeof(line), "%s", action);
    drawFittedText(statusDisplay.getDisplayHeight() - 17, line);
    snprintf(line, sizeof(line), "%s", holdAction);
    drawFittedText(statusDisplay.getDisplayHeight() - 3, line);
    statusDisplay.sendBuffer();
  }

  ButtonAction centerButtonAction() {
    const bool pressed = digitalRead(BUTTON_PIN_CENTER) == HIGH;
    if (pressed != rawButtonState) {
      rawButtonState = pressed;
      buttonChangedMs = millis();
    }
    if (pressed != stableButtonState && millis() - buttonChangedMs >= 20) {
      const bool wasPressed = stableButtonState;
      stableButtonState = pressed;
      if (stableButtonState) {
        buttonPressedMs = millis();
        buttonHoldHandled = false;
      } else if (wasPressed && !buttonHoldHandled) {
        return ButtonAction::Click;
      }
    }
    if (stableButtonState && !buttonHoldHandled &&
        millis() - buttonPressedMs >= POWER_OFF_HOLD_MS) {
      buttonHoldHandled = true;
      return ButtonAction::PowerOff;
    }
    return ButtonAction::None;
  }

  void makePath(char* path, size_t capacity) {
    const int64_t utcSeconds = recorderSensors.gpsUtcUs() / 1000000;
    time_t raw = static_cast<time_t>(utcSeconds);
    tm calendar{};
    gmtime_r(&raw, &calendar);
    strftime(path, capacity, "/raw/RAW_%Y%m%d_%H%M%S.lraw", &calendar);
    if (!SD_MMC.exists(path)) return;
    for (uint16_t suffix = 1; suffix < 1000; ++suffix) {
      snprintf(path, capacity, "/raw/RAW_%04d%02d%02d_%02d%02d%02d-%u.lraw",
               calendar.tm_year + 1900, calendar.tm_mon + 1, calendar.tm_mday, calendar.tm_hour,
               calendar.tm_min, calendar.tm_sec, suffix);
      if (!SD_MMC.exists(path)) return;
    }
  }

  bool startRecording() {
    if (!sdMounted || !recorderSensors.gpsLocked()) return false;
    makePath(lastPath, sizeof(lastPath));
    StartMetadata metadata;
    metadata.hardware = LeafVersionInfo::hardwareVariant();
    metadata.firmware = LeafVersionInfo::firmwareVersion();
    metadata.startUtcUs = recorderSensors.gpsUtcUs();
    metadata.startMonotonicUs = esp_timer_get_time();
    metadata.sensorMask = SensorBarometer | SensorImu | SensorGps | SensorAmbient | SensorPower;
    memcpy(metadata.baroCalibration, recorderSensors.baroCalibration(),
           sizeof(metadata.baroCalibration));
    metadata.imuRateHz = IMU_RATE_HZ;
    metadata.baroRateHz = BARO_RATE_HZ;
    esp_read_mac(metadata.deviceMac, ESP_MAC_WIFI_STA);
    if (!flightTape.start(lastPath, metadata)) return false;
    static constexpr char START_EVENT[] = "recording_started";
    flightTape.appendBytes(RecordType::Event, START_EVENT, sizeof(START_EVENT) - 1,
                           metadata.startMonotonicUs);
    const TimeSyncPayload sync{
        metadata.startUtcUs, recorderSensors.gpsFixQuality(), recorderSensors.satellites(), {}};
    flightTape.append(RecordType::TimeSync, sync, metadata.startMonotonicUs);
    state = RecorderState::Recording;
    return true;
  }

  void stopRecording() {
    static constexpr char STOP_EVENT[] = "recording_stopped";
    flightTape.appendBytes(RecordType::Event, STOP_EVENT, sizeof(STOP_EVENT) - 1,
                           esp_timer_get_time());
    if (flightTape.stop()) {
      state = RecorderState::Stopped;
    } else {
      state = RecorderState::Error;
      errorMessage = "SD FLUSH FAILED";
    }
  }

  [[noreturn]] void powerOff() {
    statusDisplay.clearBuffer();
    statusDisplay.setFont(u8g2_font_7x14B_tf);
    statusDisplay.drawStr(0, 32, "FINALIZING...");
    statusDisplay.setFont(u8g2_font_6x12_tf);
    statusDisplay.drawStr(0, 52, "Please wait");
    statusDisplay.sendBuffer();

    if (flightTape.recording()) {
      static constexpr char POWER_OFF_EVENT[] = "power_off";
      flightTape.appendBytes(RecordType::Event, POWER_OFF_EVENT, sizeof(POWER_OFF_EVENT) - 1,
                             esp_timer_get_time());
    }
    const bool saved = flightTape.stop(10000);

    statusDisplay.clearBuffer();
    statusDisplay.setFont(u8g2_font_7x14B_tf);
    statusDisplay.drawStr(0, 36, "POWERING OFF");
    statusDisplay.setFont(u8g2_font_6x12_tf);
    statusDisplay.drawStr(0, 58, saved ? "Recording saved" : "SD write incomplete");
    statusDisplay.sendBuffer();
    delay(750);
    statusDisplay.clearBuffer();
    statusDisplay.sendBuffer();
    digitalWrite(POWER_LATCH_PIN, LOW);
    while (true) delay(1000);  // USB power may keep the MCU alive after the latch opens.
  }
}  // namespace

void setup() {
  initializeBoard();
  drawStatus();

  if (!flightTape.begin()) {
    state = RecorderState::Error;
    errorMessage = "WRITER TASK FAILED";
    drawStatus();
    return;
  }
  mountSdCard();
  if (!recorderSensors.begin()) {
    state = RecorderState::Error;
    errorMessage = "SENSOR INIT FAILED";
  } else {
    state = RecorderState::WaitingForGps;
  }
  drawStatus();
}

void loop() {
  recorderSensors.update();
  const uint64_t nowUs = esp_timer_get_time();
  flightTape.service(nowUs);

  if (!sdMounted && millis() - lastMountAttemptMs >= 1000) {
    lastMountAttemptMs = millis();
    mountSdCard();
  }
  if (state == RecorderState::WaitingForGps && recorderSensors.gpsLocked() && sdMounted) {
    state = RecorderState::Ready;
  }
  if (state == RecorderState::Ready && !recorderSensors.gpsLocked()) {
    state = RecorderState::WaitingForGps;
  }
  if (state == RecorderState::Recording && !flightTape.healthy()) {
    stopRecording();
    state = RecorderState::Error;
    errorMessage = "SD WRITE FAILED";
  }
  if (state == RecorderState::Error && !flightTape.recording()) {
    flightTape.stop(0);  // Close after a write which outlived the normal stop timeout.
  }
  const ButtonAction buttonAction = centerButtonAction();
  if (buttonAction == ButtonAction::PowerOff) powerOff();
  if (buttonAction == ButtonAction::Click) {
    if (state == RecorderState::Ready || state == RecorderState::Stopped) {
      if (!startRecording()) {
        state = RecorderState::Error;
        errorMessage = "FILE CREATE FAILED";
      }
    } else if (state == RecorderState::Recording) {
      stopRecording();
    }
  }
  if (millis() - lastDisplayMs >= 250) {
    lastDisplayMs = millis();
    drawStatus();
  }
  delay(1);
}
