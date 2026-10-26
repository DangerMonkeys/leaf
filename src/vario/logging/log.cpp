
#include "logging/log.h"

#include <Arduino.h>

#include "comms/fanet_radio.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logbook/flight.h"
#include "logbook/igc.h"
#include "logbook/logbook_entry.h"
#include "logging/flight_auto_start.h"
#include "navigation/gpx.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/display/pages/dialogs/page_alert_timerAutoStop.h"
#include "ui/display/pages/dialogs/page_flight_summary.h"
#include "ui/display/pages/fanet/page_fanet_stats.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"
#include "wind_estimate/wind_estimate.h"

// This file keeps track of logging a flight to persistent storage.  As the operator may
// wish to start their flight and flight log before we have a "fix", know the time or
// location, we keep track of their intent to log, and start the flight record when we have
// a fix and a date/time record.
Flight* flight =
    NULL;  // Pointer to the current flight record (null if we're not deisred to be logging)
Igc igcFlight;
bool trackLogEnabledForFlight = false;

// TODO:  Delete ME

// used to keep track of current flight statistics
FlightStats logbook;
LogbookEntryFile logbookEntry;

// Alert page to warn if auto-stop is about to occur
PageAlertTimerAutoStop pageAlertTimerAutoStop;

namespace {
  FlightAutoStart autoStartDetector;
  FlightStartHistory startHistory;

  FlightAutoStart::Sample autoStartSample() {
    FlightAutoStart::Sample sample;
    sample.speedValid = gps.hasFreshGroundSpeed();
    if (sample.speedValid) sample.speedMph = gps.speed.mph();
    sample.climbValid = baro.pressureFresh() && baro.climbRate1SecAverageValid();
    if (sample.climbValid) sample.climbMps = baro.climbRate1SecAverage() / 100.0f;
    return sample;
  }

  void captureFirstGpsFixForLogbook() {
    GPSPositionSnapshot fix;
    if (!gps.lastValidFix(fix)) return;

    logbook.gpsalt_start = fix.altitudeM;
    logbook.gpsalt = logbook.gpsalt_start;
    logbook.gpsalt_above_launch = 0;
    logbook.gpsalt_max = logbook.gpsalt_start;
    logbook.gpsalt_min = logbook.gpsalt_start;
    logbook.gpsalt_above_launch_max = 0;
    logbook.speed = fix.speedMps;
    logbook.speed_max = logbook.speed;
    logbook.startLocationLat = fix.latitude;
    logbook.startLocationLng = fix.longitude;
    logbookEntry.captureFirstFix(logbook);
  }

  // Rough in-flight check for g-load statistics: moving over the ground faster than walking, or
  // climbing/sinking noticeably (covers flying with little ground speed in strong wind).

  GLoadDiagnostics captureGLoadDiagnostics() {
    GLoadDiagnostics d;
    d.checkMs = millis();
    d.speedValid = gps.hasUsableFix() && gps.hasFreshGroundSpeed();
    if (d.speedValid) {
      d.speedMps = gps.speed.mps();
      d.fast = gps.speed.mps() > GLoadDiagnostics::MIN_SPEED_MPS;
    }
    d.climbValid = baro.state() == Barometer::State::Ready && baro.climbRateFilteredValid();
    if (d.climbValid) {
      d.climbCms = baro.climbRateFiltered();
      d.vertical = abs(d.climbCms) > GLoadDiagnostics::MIN_CLIMB_CMS;
    }
    d.rawValid = imu.accelValid();
    if (d.rawValid) d.rawG = imu.getAccel();
    d.rawSampleMs = imu.lastMotionTime();
    return d;
  }

  void captureGLoadExtrema() {
    logbook.accel_max = imu.gLoad().valid() ? imu.gLoad().maxG() : NAN;
    logbook.accel_min = imu.gLoad().valid() ? imu.gLoad().minG() : NAN;
  }

  void writeGLoadSnapshot(const GLoadDiagnostics& d) {
    if (trackLogEnabledForFlight && flight == &igcFlight && igcFlight.started())
      igcFlight.logGLoadDiagnostics(d);
    else
      imu.gLoad().takeInterval();  // Keep intervals bounded even without a track file.
  }
}  // namespace

//////////////////////////////////////////////////////////////////////////////////////////
// UPDATE - Main function to run every second
void log_update() {
  // Check auto-start criteria if we haven't begun a flight yet
  if (!flight) {
    // Always sample while idle, including when auto-start is disabled, so manual
    // flights also retain their recent launch inputs.
    if (flightTimer_autoStart()) flightTimer_start(FlightStartCause::Automatic);
    // Check auto-stop criteria if we have started a flight (and AutoStop is ON)
  } else {
    if (settings.log_autoStop) {
      if (flightTimer_autoStop()) flightTimer_stop(true, FlightEndCause::Automatic);
    }
  }

  // Now do all the regular log stuff if we have an active log
  if (flight) {
    // We're currently logging a flight (we're flying)

    // Current second of the flight
    auto currentSecondSinceBoot = millis() / 1000;

    // Current second of the flight
    logbook.duration = currentSecondSinceBoot - logbook.logStartedAt;
    const GLoadDiagnostics gLoadDiagnostics = captureGLoadDiagnostics();
    imu.gLoad().updateAirborne(gLoadDiagnostics.looksAirborne(),
                               gLoadDiagnostics.detectorInputsValid());
    captureGLoadExtrema();
    logbookEntry.refreshStartTimeFromSyncedClock(logbook);

    // We wish to save a tracklog, but the tracklog has not yet started.
    if (trackLogEnabledForFlight && !flight->started()) {
      if (!gps.hasUsableFix()) {
        // We don't have a valid GPS location yet, try again later
        logbookEntry.writePlaceholder(logbook);
        writeGLoadSnapshot(gLoadDiagnostics);
        return;
      }

      if (baro.state() != Barometer::State::Ready) {
        // The baro is still discarding its startup samples, so it can't set a launch altitude
        // yet; try again later
        logbookEntry.writePlaceholder(logbook);
        writeGLoadSnapshot(gLoadDiagnostics);
        return;
      }

      // We have a GPS fix, we're able to start recording of the flight.
      // Do all the necessary starting actions as we start the recording.
      // TODO:  A second sound effect to show that recording has now started??
      if (flight->startFlight()) {
        // TODO:  Make this sound much cooler
        speaker.playSound(fx::started);

        // if Altimeter GPS-SYNC is on, reset altimeter setting
        // so baro matches GPS when log is started
        if (settings.vario_altSyncToGPS) baro.syncToGPSAlt();

        // starting values
        baro.setLaunchAlt();
        logbook.alt_start = baro.altAtLaunch();
        captureFirstGpsFixForLogbook();
        logbookEntry.setTrack(flight->trackLogFormat(), flight->trackLogPath());

        // get first set of log values
        log_captureValues();

        // initial min/max values
        logbook.alt_max = logbook.alt_start;
        logbook.alt_min = logbook.alt_start;
        logbook.alt_above_launch_max = 0;
        logbook.climb_max = logbook.climb_min = 0;
        logbook.speed_max = 0;
        logbook.temperature_max = logbook.temperature_min = logbook.temperature;
      }
    }

    if (!trackLogEnabledForFlight && gps.hasUsableFix() && logbook.startLocationLat == 0 &&
        logbook.startLocationLng == 0) {
      captureFirstGpsFixForLogbook();
    }

    // Coalesce clock, first-fix and track association before the single milestone write.
    logbookEntry.writePlaceholder(logbook);
    // Generate a tracklog record.
    if (trackLogEnabledForFlight && gps.hasUsableFix()) flight->log(logbook.duration);
    log_captureValues();      // TODO:  Update this to an "Update Flight Stats" or something
    log_checkMinMaxValues();  // TODO:  Probably rename this to be "bound Flight Stats"
    writeGLoadSnapshot(gLoadDiagnostics);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Auto Start & Stop check functions.

uint8_t autoStopCounter = 0;
int32_t autoStopAltitude = 0;

void flightTimer_resetAutoStart() { autoStartDetector.reset(); }

bool flightTimer_autoStart() {
  if (flight) return false;
  const auto sample = autoStartSample();
  const bool inhibited = PageFlightSummary::isShowing();
  bool start = false;
  if (!settings.log_autoStart || inhibited) {
    autoStartDetector.reset();
  } else {
    start = autoStartDetector.update(millis(), sample);
  }
  startHistory.record(millis(), sample, autoStartDetector.score(), settings.log_autoStart,
                      inhibited);
  if (start) Serial.println("****************************** autoStart TRUE via motion score");
  return start;
}

void log_writeStartDiagnostics(IgcLogger& logger) {
  if (!startHistory.frozen()) return;
  char line[80];
  snprintf(line, sizeof(line), "START V=1 CAUSE=%s T_MS=%lu N=%u",
           startHistory.cause() == FlightStartCause::Automatic ? "AUTO" : "MANUAL",
           static_cast<unsigned long>(startHistory.startedAtMs()),
           static_cast<unsigned>(startHistory.size()));
  logger.writeLRecord(line);
  snprintf(line, sizeof(line), "STARTCFG SPEED_MPH=%.2f CLIMB_MPS=%.2f MEMORY_S=%.2f MIN_MS=%lu",
           FlightAutoStart::SPEED_SCALE_MPH, FlightAutoStart::CLIMB_SCALE_MPS,
           FlightAutoStart::MEMORY_SECONDS,
           static_cast<unsigned long>(FlightAutoStart::MIN_DETECTION_MS));
  logger.writeLRecord(line);
  logger.writeLRecord("STARTFIELDS DT_MS V=MPH C=M/S S=SCORE F=1:SPEED,2:CLIMB,4:ON,8:BLOCK");
  for (size_t i = 0; i < startHistory.size(); ++i) {
    const auto& entry = startHistory.at(i);
    char speed[16] = "NA", climb[16] = "NA";
    if (entry.flags & FlightStartHistory::SpeedValid)
      snprintf(speed, sizeof(speed), "%.4g", entry.speedMph);
    if (entry.flags & FlightStartHistory::ClimbValid)
      snprintf(climb, sizeof(climb), "%.4g", entry.climbMps);
    const int32_t relativeMs = -static_cast<int32_t>(startHistory.startedAtMs() - entry.atMs);
    snprintf(line, sizeof(line), "STARTDATA DT_MS=%ld V=%s C=%s S=%.6f F=%02X",
             static_cast<long>(relativeMs), speed, climb, entry.score, entry.flags);
    logger.writeLRecord(line);
  }
}

bool showingAlert_ = false;

bool flightTimer_autoStop() {
  if (baro.state() != Barometer::State::Ready) {
    return false;  // can't autoStop without the Baro
  }

  int32_t altDifference = abs(baro.alt() - autoStopAltitude);

  const bool gpsSpeedQuiet =
      gps.hasUsableFix() ? gps.hasFreshGroundSpeed() && gps.speed.mph() < AUTO_STOP_MAX_SPEED
                         : true;

  if ((altDifference < AUTO_STOP_MAX_ALT) &&  // Check if altitude is stable
      gpsSpeedQuiet &&                        // And GPS speed is slow enough, if current
      (imu.accelValid() && abs(imu.getAccel() - 1.0f) < AUTO_STOP_MAX_ACCEL)) {  // and IMU is calm

    // if all three conditions are met, increment the counter
    autoStopCounter++;
    // alert the user if we've been in this state for a little while and are about to stop
    if (autoStopCounter == AUTO_STOP_MIN_SEC / 2) {
      pageAlertTimerAutoStop.show();
      showingAlert_ = true;
      speaker.playSound(fx::cancel);
    }
    // and check if we've been in this state long enough to trigger auto-stop
    if (autoStopCounter >= AUTO_STOP_MIN_SEC) {
      autoStopCounter = 0;  // reset counter for next time
      if (showingAlert_) {
        pageAlertTimerAutoStop.closeAlert();
        showingAlert_ = false;
      }
      return true;
    }
  } else {
    autoStopCounter = 0;
    if (showingAlert_) {
      pageAlertTimerAutoStop.closeAlert();
      showingAlert_ = false;
    }

    // reset the comparison altitude to present altitude, since it's still changing
    autoStopAltitude = baro.alt();
  }
  return false;
}

uint8_t flightTimer_getAutoStopCountRemaining() {
  if (autoStopCounter)
    return AUTO_STOP_MIN_SEC - autoStopCounter;
  else
    return 0;
}

void flightTimer_resetAutoStop() {
  autoStopCounter = 0;
  showingAlert_ = false;
}

//////////////////////////////////////////////////////////////////////////////////
// FLight Timer Management Functions

// check if running
bool flightTimer_isRunning() { return flight != NULL; }
bool flightTimer_ownsLogbookPath(const String& path) {
  if (!flight) return false;
  return path == logbookEntry.path() || path.endsWith("_" + logbookEntry.flightId() + ".json");
}
bool flightTimer_isLogging() {
  return flightTimer_isRunning() && (!trackLogEnabledForFlight || (bool)flight->started());
}

void flightTimer_markSavedPoint(const Waypoint& waypoint) {
  if (flight != &igcFlight || !trackLogEnabledForFlight || !igcFlight.started()) return;
  igcFlight.markSavedPoint(waypoint);
}

// start timer
void flightTimer_start(FlightStartCause cause) {
  // Short-circuit if a flight is already started
  if (flight != NULL) {
    return;
  }

  // Release an idle repair iterator before this flight can create any files.
  LogbookStore::beginRepairSession(false);

  // Freeze before resetting the detector, including its final triggering score.
  startHistory.record(millis(), autoStartSample(), autoStartDetector.score(),
                      settings.log_autoStart, PageFlightSummary::isShowing());
  startHistory.freeze(millis(), cause);
  autoStartDetector.reset();
  flightTimer_resetAutoStop();

  // start timer
  speaker.playSound(fx::enter);
  settings.resetShortcutVolume();
  flight = &igcFlight;
  display.resetThermalState();
  imu.gLoad().startFlight();

  logbook.logStartedAt = millis() / 1000;
  log_captureValues();
  logbook.temperature_max = logbook.temperature_min = logbook.temperature;
  logbookEntry.begin(logbook);
  trackLogEnabledForFlight = settings.log_saveTrack;

  // Start the Fanet radio
  fanetRadio.begin(settings.fanet_region);
}

// stop timer
void flightTimer_stop(bool showSummary, FlightEndCause cause) {
  autoStartDetector.reset();
  startHistory.reset();
  windEstimator.clearWindEstimate();  // clear the wind estimate when we stop a flight
  power.resetAutoOffCounter();  // reset the auto-off counter when we stop a flight (it could have
                                // counted up to nearly the limit prior to auto-starting a log)
  flightTimer_resetAutoStop();
  // Also clear non-logging thermal sessions and repeated stop/shutdown requests.
  display.resetThermalState();
  // Short Circuit, no need to do anything if there's no flight recording.
  if (flight == NULL) {
    return;
  }

  const bool previouslyMuted = speaker.muted();
  speaker.mute();  // Stop hardware output before any final IGC or JSON file work.

  // ending values
  imu.gLoad().stopFlight();
  captureGLoadExtrema();
  auto gLoadDiagnostics = captureGLoadDiagnostics();
  gLoadDiagnostics.ending = true;
  writeGLoadSnapshot(gLoadDiagnostics);  // Includes pending values discarded at stop, before G.
  log_captureEndingValues();

  const String trackFormat = flight->started() ? flight->trackLogFormat() : "";
  const String trackPath = flight->started() ? flight->trackLogPath() : "";

  // Close the tracklog before finalizing the logbook reference to it.
  if (flight->started()) {
    const char* causeName = "MANUAL";
    switch (cause) {
      case FlightEndCause::Automatic:
        causeName = "AUTO";
        break;
      case FlightEndCause::PowerOff:
        causeName = "POWER_OFF";
        break;
      case FlightEndCause::LowBattery:
        causeName = "LOW_BATTERY";
        break;
      case FlightEndCause::Manual:
        break;
    }
    igcFlight.markEnd(causeName);
    flight->end(logbook, false);
  }

  u8g2.firstPage();
  do {
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(0, 85);
    u8g2.print("Saving flight...");
  } while (u8g2.nextPage());
  const bool summarySaved = logbookEntry.finalize(logbook, trackFormat, trackPath);
  if (!summarySaved && !showSummary) {
    u8g2.firstPage();
    do {
      u8g2.setFont(leaf_6x12);
      u8g2.setCursor(0, 75);
      u8g2.print("Logbook save");
      u8g2.setCursor(0, 90);
      u8g2.print("failed");
      u8g2.setCursor(0, 115);
      u8g2.print(trackPath.isEmpty() ? "No track file" : "Track unverified");
    } while (u8g2.nextPage());
    delay(1000);  // Visible outcome before shutdown clears the display; no acknowledgement.
  }
  if (showSummary) {
    static PageFlightSummary dialog;
    LogbookEntrySummary snapshot;
    logbookEntry.summary(logbook, snapshot);
    dialog.show(logbook, logbookEntry.path(), trackPath, summarySaved, &snapshot);
  }
  logbookEntry.reset();
  // TODO:  A much cooler end flight sound.  Perhaps even an easter egg?
  if (!previouslyMuted) speaker.unMute();
  speaker.playSound(summarySaved ? fx::confirm : fx::bad);
  settings.resetShortcutVolume();
  flight = NULL;
  trackLogEnabledForFlight = false;
  logbook = FlightStats();  // Reset the flight stats

  // Stop the Fanet radio
  fanetRadio.end();

  // reset initial alt
  // (to enable properly checking again for auto-start conditions now that timer is stopped)
  if (baro.state() == Barometer::State::Ready) baro.setAltInitial();
}

void flightTimer_toggle() {
  if (flight)
    flightTimer_stop();
  else
    flightTimer_start();
}

String flightTimer_getString() {
  // If the flight has not yet started logging, just make it flash empty every half second.
  if (flightTimer_isRunning() && !flightTimer_isLogging()) {
    if ((millis() / 500) % 2) return "";
  }
  // Assume that the flight Timer's boxes are always 6 characters wide
  return formatSeconds(logbook.duration, true, 6);
}

void log_captureValues() {
  if (baro.state() == Barometer::State::Ready) {
    logbook.alt = baro.altAdjusted();
    logbook.alt_above_launch = baro.altAboveLaunch();

    if (baro.climbRateFilteredValid()) logbook.climb = baro.climbRateFiltered();
  }

  if (gps.hasUsableFix()) {
    GPSPositionSnapshot fix;
    if (gps.lastValidFix(fix)) {
      logbook.speed = fix.speedMps;

      logbook.gpsalt = fix.altitudeM;
      logbook.gpsalt_above_launch = fix.altitudeM - logbook.gpsalt_start;

      // accumulate distance flown
      logbook.distanceAlongPath += fix.speedMps;
    }
  }

  if (ambient.state() == Ambient::State::Ready) {
    logbook.temperature = ambient.temp();
  }

  if (imu.accelValid()) {
    logbook.accel = imu.getAccel();
  }
}

void log_captureEndingValues() {
  if (baro.state() == Barometer::State::Ready) {
    logbook.alt_end = baro.alt();
  }

  GPSPositionSnapshot fix;
  if (gps.lastValidFix(fix)) {
    logbook.gpsalt_end = fix.altitudeM;
    logbook.endLocationLat = fix.latitude;
    logbook.endLocationLng = fix.longitude;

    if (logbook.startLocationLat != 0 || logbook.startLocationLng != 0) {
      logbook.distanceStraightLine =
          gps.distanceBetween(logbook.startLocationLat, logbook.startLocationLng,
                              logbook.endLocationLat, logbook.endLocationLng);
    }
  }
}

void log_checkMinMaxValues() {
  uint32_t time = micros();

  if (baro.state() == Barometer::State::Ready) {
    // check altitude values for log records
    if (logbook.alt > logbook.alt_max) {
      logbook.alt_max = logbook.alt;
      if (logbook.alt_above_launch > logbook.alt_above_launch_max)
        logbook.alt_above_launch_max =
            logbook.alt_above_launch;  // we only need to check for max above-launch values if we're
                                       // also setting a new altitude max.
    } else if (logbook.alt < logbook.alt_min) {
      logbook.alt_min = logbook.alt;
    }

    if (baro.climbRateFilteredValid()) {
      // check climb values for log records
      if (logbook.climb > logbook.climb_max) {
        logbook.climb_max = logbook.climb;
      } else if (logbook.climb < logbook.climb_min) {
        logbook.climb_min = logbook.climb;
      }
    }
  }

  if (gps.hasUsableFix()) {
    if (logbook.gpsalt > logbook.gpsalt_max) {
      logbook.gpsalt_max = logbook.gpsalt;
      if (logbook.gpsalt_above_launch > logbook.gpsalt_above_launch_max)
        logbook.gpsalt_above_launch_max =
            logbook.gpsalt_above_launch;  // we only need to check for max above-launch values if
                                          // we're also setting a new gps altitude max.
    } else if (logbook.gpsalt < logbook.gpsalt_min) {
      logbook.gpsalt_min = logbook.gpsalt;
    }
  }

  // check temperature values for log records
  if (ambient.state() == Ambient::State::Ready) {
    if (logbook.temperature > logbook.temperature_max) {
      logbook.temperature_max = logbook.temperature;
    } else if (logbook.temperature < logbook.temperature_min) {
      logbook.temperature_min = logbook.temperature;
    }
  }

  // Check speed value for log records
  if (logbook.speed > logbook.speed_max) {
    logbook.speed_max = logbook.speed;
  }

  const WindEstimate& windEstimate = windEstimator.getWindEstimate();
  if (windEstimate.validEstimate && windEstimate.windSpeed > logbook.windSpeedMax) {
    logbook.windValid = true;
    logbook.windSpeedMax = windEstimate.windSpeed;
    logbook.windDirectionFromAtMaxDeg =
        fmodf(windEstimate.windDirectionFrom * RAD_TO_DEG + 360.0f, 360.0f);
  }

  // time = micros() - time;
  // Serial.print("checkMinMax: ");
  // Serial.println(time);
}
