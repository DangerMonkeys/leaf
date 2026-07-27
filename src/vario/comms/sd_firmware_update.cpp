#include "comms/sd_firmware_update.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>

#include "diagnostics/heap_monitor.h"
#include "system/version_info.h"
#include "ui/settings/settings.h"

namespace {
  constexpr const char* UPDATE_DIR = "/firmware";
  constexpr const char* STATUS_FILE = "/firmware/update_status.txt";
  constexpr const char* STATE_NAMESPACE = "sdFwUpdate";
  constexpr const char* STATE_KEY = "state";
  constexpr const char* STATE_IDLE = "idle";
  constexpr const char* STATE_ATTEMPTING = "attempting";
  constexpr const char* STATE_INSTALLED = "installed";
  constexpr size_t READ_BUFFER_SIZE = 1024;
  constexpr size_t MIN_APP_IMAGE_SIZE = 4096;
  constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;

  enum class Result {
    SkippedNoFile,
    FailedAttemptAlreadyPending,
    FailedMultipleFiles,
    FailedFilenameHardwareMissing,
    FailedFilenameHardwareMismatch,
    FailedOpen,
    FailedEmptyOrTooSmall,
    FailedTooLarge,
    FailedInvalidEspImage,
    FailedBinaryMarkerMissing,
    FailedBinaryHardwareMismatch,
    FailedUpdateBegin,
    FailedUpdateWrite,
    FailedUpdateEnd,
    Installed
  };

  Preferences updatePrefs;

  String hardwareVariant() { return String(LeafVersionInfo::hardwareVariant()); }

  String hardwareVersionToken() {
    String token = hardwareVariant();
    if (token.startsWith("leaf_")) token.remove(0, 5);
    token.replace('_', '.');
    return token;
  }

  String hardwareMarker() { return String("LEAF_HARDWARE_VARIANT=") + hardwareVariant(); }

  String firmwareVersionHardwareMarker() { return String("+h") + hardwareVersionToken(); }

  String state() {
    updatePrefs.begin(STATE_NAMESPACE, true);
    String value = updatePrefs.getString(STATE_KEY, STATE_IDLE);
    updatePrefs.end();
    return value;
  }

  void setState(const char* value) {
    updatePrefs.begin(STATE_NAMESPACE, false);
    updatePrefs.putString(STATE_KEY, value);
    updatePrefs.end();
  }

  bool isNumericTokenBoundary(char c) { return c == '\0' || (!isDigit(c) && c != '.'); }

  bool containsNumericToken(const String& haystack, const String& token) {
    int index = haystack.indexOf(token);
    while (index >= 0) {
      const int before = index - 1;
      const int after = index + token.length();
      const char beforeChar = before >= 0 ? haystack[before] : '\0';
      const char afterChar = after < static_cast<int>(haystack.length()) ? haystack[after] : '\0';
      if (isNumericTokenBoundary(beforeChar) && isNumericTokenBoundary(afterChar)) return true;
      index = haystack.indexOf(token, index + 1);
    }
    return false;
  }

  bool containsVariantToken(const String& haystack, const String& token) {
    int index = haystack.indexOf(token);
    while (index >= 0) {
      const int after = index + token.length();
      const char afterChar = after < static_cast<int>(haystack.length()) ? haystack[after] : '\0';
      if (!isDigit(afterChar)) return true;
      index = haystack.indexOf(token, index + 1);
    }
    return false;
  }

  bool containsHardwareLikeVersion(const String& haystack) {
    for (size_t i = 0; i + 4 < haystack.length(); i++) {
      if (!isDigit(haystack[i])) continue;
      if (haystack[i + 1] != '.' || !isDigit(haystack[i + 2]) || haystack[i + 3] != '.' ||
          !isDigit(haystack[i + 4])) {
        continue;
      }
      return true;
    }
    return false;
  }

  bool hasFirmwareExtension(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".bin");
  }

  bool filenameContainsDifferentLeafHardware(const String& name) {
    return name.indexOf("leaf_") >= 0 || name.indexOf("Leaf_") >= 0 || name.indexOf("LEAF_") >= 0;
  }

  Result validateFilename(const String& path) {
    String name = path.substring(path.lastIndexOf('/') + 1);
    String lower = name;
    lower.toLowerCase();

    String variant = hardwareVariant();
    variant.toLowerCase();
    String version = hardwareVersionToken();

    if (containsVariantToken(lower, variant) || containsNumericToken(lower, version)) {
      return Result::Installed;
    }

    if (filenameContainsDifferentLeafHardware(lower) || containsHardwareLikeVersion(lower)) {
      return Result::FailedFilenameHardwareMismatch;
    }
    return Result::FailedFilenameHardwareMissing;
  }

  const char* resultTitle(Result result) {
    switch (result) {
      case Result::SkippedNoFile:
        return "skipped";
      case Result::Installed:
        return "installed";
      default:
        return "failed";
    }
  }

  const char* resultReason(Result result) {
    switch (result) {
      case Result::SkippedNoFile:
        return "no firmware file was found.";
      case Result::FailedAttemptAlreadyPending:
        return "a previous SD firmware update attempt did not finish cleanly.";
      case Result::FailedMultipleFiles:
        return "multiple firmware .bin files were found.";
      case Result::FailedFilenameHardwareMissing:
        return "firmware filename did not contain this Leaf hardware version.";
      case Result::FailedFilenameHardwareMismatch:
        return "firmware filename appears to be for different hardware.";
      case Result::FailedOpen:
        return "firmware file could not be opened.";
      case Result::FailedEmptyOrTooSmall:
        return "firmware file was empty or too small.";
      case Result::FailedTooLarge:
        return "firmware file is too large for the update partition.";
      case Result::FailedInvalidEspImage:
        return "firmware file was not a valid ESP32 application image.";
      case Result::FailedBinaryMarkerMissing:
        return "firmware binary did not contain a Leaf hardware marker.";
      case Result::FailedBinaryHardwareMismatch:
        return "firmware binary appears to be for different hardware.";
      case Result::FailedUpdateBegin:
        return "Leaf could not start the firmware update.";
      case Result::FailedUpdateWrite:
        return "Leaf could not write the firmware update.";
      case Result::FailedUpdateEnd:
        return "Leaf could not finish or validate the firmware update.";
      case Result::Installed:
        return "firmware was installed and Leaf rebooted.";
    }
    return "unknown firmware update result.";
  }

  void writeStatus(Result result, const String& path, const String& detail = "") {
    SD_MMC.mkdir(UPDATE_DIR);
    File status = SD_MMC.open(STATUS_FILE, FILE_WRITE);
    if (!status) return;

    status.println("Leaf firmware update status");
    status.println();
    status.print("Result: ");
    status.println(resultTitle(result));
    status.print("Reason: ");
    status.println(resultReason(result));
    status.print("Leaf hardware: ");
    status.println(hardwareVariant());
    status.print("Expected filename token: ");
    status.print(hardwareVersionToken());
    status.print(" or ");
    status.println(hardwareVariant());
    if (path.length()) {
      status.print("File: ");
      status.println(path);
    }
    if (detail.length()) {
      status.print("Detail: ");
      status.println(detail);
    }
    status.print("Action: ");
    status.println(result == Result::Installed ? "firmware was installed and Leaf rebooted."
                                               : "no firmware was installed.");
    status.close();
  }

  String firstFirmwareCandidate(Result& result) {
    result = Result::SkippedNoFile;
    File dir = SD_MMC.open(UPDATE_DIR, FILE_READ);
    if (!dir || !dir.isDirectory()) return "";

    String found;
    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory() && hasFirmwareExtension(entry.name())) {
        if (found.length()) {
          result = Result::FailedMultipleFiles;
          entry.close();
          dir.close();
          return "";
        }
        found = entry.path();
        result = Result::Installed;
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
    return found;
  }

  bool bufferContains(const uint8_t* buffer, size_t len, const String& needle) {
    const size_t needleLen = needle.length();
    if (needleLen == 0 || len < needleLen) return false;

    for (size_t i = 0; i <= len - needleLen; i++) {
      if (memcmp(buffer + i, needle.c_str(), needleLen) == 0) return true;
    }
    return false;
  }

  Result validateBinaryMarkers(File& firmware, String& detail) {
    uint8_t buffer[READ_BUFFER_SIZE + 64];
    size_t carry = 0;
    bool sawLeafMarker = false;
    bool sawVersionHardwareMarker = false;
    const String expectedMarker = hardwareMarker();
    const String expectedVersionMarker = firmwareVersionHardwareMarker();

    firmware.seek(0);
    while (firmware.available()) {
      const size_t readLen = firmware.read(buffer + carry, READ_BUFFER_SIZE);
      const size_t total = carry + readLen;
      if (bufferContains(buffer, total, expectedMarker) ||
          bufferContains(buffer, total, expectedVersionMarker)) {
        firmware.seek(0);
        return Result::Installed;
      }
      if (bufferContains(buffer, total, "LEAF_HARDWARE_VARIANT=leaf_")) sawLeafMarker = true;
      if (bufferContains(buffer, total, "+h")) sawVersionHardwareMarker = true;

      carry = total < 63 ? total : 63;
      memmove(buffer, buffer + total - carry, carry);
    }

    firmware.seek(0);
    if (sawLeafMarker || sawVersionHardwareMarker) {
      detail = "Expected " + expectedMarker + " or " + expectedVersionMarker + ".";
      return Result::FailedBinaryHardwareMismatch;
    }

    detail = "Expected " + expectedMarker + " or " + expectedVersionMarker + ".";
    return Result::FailedBinaryMarkerMissing;
  }

  const char* archiveSuffix(Result result) {
    switch (result) {
      case Result::Installed:
        return "installed";
      case Result::FailedFilenameHardwareMissing:
      case Result::FailedFilenameHardwareMismatch:
      case Result::FailedBinaryMarkerMissing:
      case Result::FailedBinaryHardwareMismatch:
        return "incompatible";
      case Result::FailedUpdateBegin:
      case Result::FailedUpdateWrite:
      case Result::FailedUpdateEnd:
      case Result::FailedAttemptAlreadyPending:
        return "attempted";
      case Result::FailedOpen:
      case Result::FailedEmptyOrTooSmall:
      case Result::FailedTooLarge:
      case Result::FailedInvalidEspImage:
        return "invalid";
      case Result::SkippedNoFile:
      case Result::FailedMultipleFiles:
        return "";
    }
    return "attempted";
  }

  bool archiveFirmwareFile(const String& path, Result result) {
    if (!path.length() || !SD_MMC.exists(path)) return true;
    const char* suffix = archiveSuffix(result);
    if (!suffix[0]) return true;
    String archived = path + ".";
    archived += suffix;
    if (SD_MMC.exists(archived)) SD_MMC.remove(archived);
    return SD_MMC.rename(path, archived);
  }

  Result validateBasicFile(File& firmware, size_t& size) {
    size = firmware.size();
    if (size < MIN_APP_IMAGE_SIZE) return Result::FailedEmptyOrTooSmall;
    if (size > ESP.getFreeSketchSpace()) return Result::FailedTooLarge;

    firmware.seek(0);
    if (firmware.read() != ESP_IMAGE_MAGIC) return Result::FailedInvalidEspImage;
    firmware.seek(0);
    return Result::Installed;
  }

  Result installFirmware(File& firmware, size_t size) {
    firmware.seek(0);
    if (!Update.begin(size)) return Result::FailedUpdateBegin;
    if (Update.writeStream(firmware) != size) {
      Update.abort();
      return Result::FailedUpdateWrite;
    }
    if (!Update.end(true)) return Result::FailedUpdateEnd;
    return Result::Installed;
  }
}  // namespace

void sd_firmware_update::handleBootUpdate() {
  heap_monitor::checkpoint("sd-fw-update-start");
  const String priorState = state();
  Result candidateResult;
  String path = firstFirmwareCandidate(candidateResult);

  if (candidateResult == Result::SkippedNoFile) {
    if (priorState == STATE_INSTALLED) setState(STATE_IDLE);
    return;
  }

  if (candidateResult == Result::FailedMultipleFiles) {
    writeStatus(candidateResult, "", "Place exactly one firmware .bin file in /firmware.");
    return;
  }

  if (priorState == STATE_ATTEMPTING) {
    archiveFirmwareFile(path, Result::FailedAttemptAlreadyPending);
    writeStatus(Result::FailedAttemptAlreadyPending, path);
    setState(STATE_IDLE);
    return;
  }

  Result filenameResult = validateFilename(path);
  if (filenameResult != Result::Installed) {
    archiveFirmwareFile(path, filenameResult);
    writeStatus(filenameResult, path);
    return;
  }

  File firmware = SD_MMC.open(path, FILE_READ);
  if (!firmware) {
    archiveFirmwareFile(path, Result::FailedOpen);
    writeStatus(Result::FailedOpen, path);
    return;
  }

  size_t size = 0;
  Result result = validateBasicFile(firmware, size);
  String detail;
  if (result == Result::Installed) result = validateBinaryMarkers(firmware, detail);

  if (result == Result::Installed) {
    setState(STATE_ATTEMPTING);
    result = installFirmware(firmware, size);
  }
  firmware.close();

  if (result == Result::Installed) {
    archiveFirmwareFile(path, result);
    writeStatus(result, path);
    settings.boot_toOnState = true;
    settings.save();
    setState(STATE_INSTALLED);
    heap_monitor::checkpoint("sd-fw-update-installed");
    ESP.restart();
  }

  archiveFirmwareFile(path, result);
  writeStatus(result, path, detail);
  setState(STATE_IDLE);
  heap_monitor::checkpoint("sd-fw-update-failed");
}
