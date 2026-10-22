#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <assert.h>
#include <stdio.h>

#include "dispatch/message_bus.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logbook/igc.h"
#include "logbook/logbook_entry.h"
#include "logging/log.h"
#include "navigation/thermal_core.h"
#include "power.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/display/pages/primary/page_simple.h"
#include "ui/display/pages/primary/page_thermal.h"
#include "ui/display/pages/primary/page_thermal_core.h"
#include "ui/display/pages/primary/page_thermal_track.h"
#include "ui/settings/settings.h"

extern MessageBus<11> bus;
extern Igc igcFlight;
extern LogbookEntryFile logbookEntry;

namespace {
  void flyFor(uint32_t ms) {
    const uint32_t start = millis();
    while (uint32_t(millis() - start) < ms) sim::runtime().step();
  }

  void assertEmpty() {
    assert(thermalTracker.recentCoreSamples().size() == 0);
    assert(thermalTracker.savedThermalCount() == 0);
    assert(thermalTracker.displayItemCount() == 0);
    assert(thermalTracker.selectedDisplayItem() == nullptr);
    const auto& estimate = thermalCore.estimate();
    assert(!estimate.valid && !estimate.centerValid && estimate.markerCount == 0);
    assert(estimate.direction == 0 && estimate.adviceQ7 == 0);
  }

  void populate() {
    sim::scenario().seek(0);
    sim::scenario().play();
    flyFor(350000);
    assert(thermalTracker.savedThermalCount() > 0);
    assert(thermalTracker.displayItemCount() > 0);
    assert(thermalCore.estimate().markerCount > 0);
    sim::scenario().pause();
  }

  void checkNoCachedFix() {
    // Refreshing the snapshot without another position sentence must not make it fresh.
    // Only a newly received position may reopen thermal collection after reset.
    for (unsigned i = 0; i < 3; ++i) {
      sim::clock().advanceUs(10000);
      gps.update();
      thermalTracker.updateDetector();
      thermalTracker.updateNavigation();
      thermalCore.update();
      assertEmpty();
    }
    flyFor(4000);  // The retained fix is now stale as well.
    assertEmpty();
    sim::scenario().play();
    flyFor(12000);  // Allow the 20 discarded 4 Hz pressure samples and IMU startup.
    assert(thermalTracker.recentCoreSamples().size() > 0);
    sim::scenario().pause();
  }

  void drawAllPages() {
    simplePage_draw();
    thermalPage_draw();
    thermalCorePage_draw();
    thermalTrackPage_draw();
    navigatePage_draw();
  }

  void selectPageCursors() {
    simplePage_button(Button::DOWN, ButtonEvent::CLICKED, 0);
    thermalPage_button(Button::DOWN, ButtonEvent::CLICKED, 0);
    thermalCorePage_button(Button::DOWN, ButtonEvent::CLICKED, 0);
    thermalTrackPage_button(Button::DOWN, ButtonEvent::CLICKED, 0);
  }

  void checkCursorsCleared() {
    for (auto button :
         {simplePage_button, thermalPage_button, thermalCorePage_button, thermalTrackPage_button}) {
      const auto altType = settings.disp_thmPageAltType;
      button(Button::CENTER, ButtonEvent::CLICKED, 0);
      assert(settings.disp_thmPageAltType == altType);
      assert(!flightTimer_isRunning());
    }
  }

  void checkUnavailableBarometer() {
    assert(thermalTracker.recentCoreSamples().size() > 0);
    baro.sleep();
    for (unsigned state = 0; state < 2; ++state) {
      assert(baro.state() != Barometer::State::Ready);
      thermalCore.update();
      assert(thermalCore.estimate().markerCount == 0);
      assert(!thermalCore.estimate().centerValid);
      for (bool feet : {false, true}) {
        settings.units_alt = feet;
        settings.disp_thmPageAltType = altType_MSL;
        settings.disp_navPageAltType = altType_MSL;
        drawAllPages();
        settings.disp_thmPageAltType = altType_aboveLaunch;
        settings.disp_navPageAltType = altType_aboveLaunch;
        drawAllPages();
      }
      // The tracklog is already open with a usable fix. Skip a record, never invent altitude.
      igcFlight.log(logbook.duration);
      if (state == 0) baro.wake();
    }
    flightTimer_stop(false);  // Also exercises stop's formerly unguarded setAltInitial().
    assertEmpty();
    flightTimer_stop(false);
    assertEmpty();
    settings.disp_thmPageAltType = altType_MSL;
    sim::scenario().play();
    flyFor(12000);
    assert(baro.state() == Barometer::State::Ready);
    assert(thermalTracker.recentCoreSamples().size() > 0);
    sim::scenario().pause();
  }

  void checkAutoSwitchReset() {
    settings.autoSwitchToThermalCorePage = true;
    display.setPage(MainPage::Navigate);
    display.updateThermalAutoSwitch(false);
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::ThermalCore);
    flightTimer_stop(false);
    display.updateThermalAutoSwitch(false);
    assert(display.getPage() == MainPage::ThermalCore);  // Discarded return-page state.
    display.setPage(MainPage::Navigate);
    display.updateThermalAutoSwitch(true);  // Fresh guidance is a new edge.
    assert(display.getPage() == MainPage::ThermalCore);
    flightTimer_stop(false);
    assert(settings.autoSwitchToThermalCorePage);
    settings.autoSwitchToThermalCorePage = false;
  }
}  // namespace

int main() {
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/thermal-lifecycle/state";
  options.cardRoot = "sim/build-tests/thermal-lifecycle/card";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.loadDefaults();
  settings.log_autoStart = settings.log_autoStop = false;
  settings.log_saveTrack = true;
  settings.system_autoOff = 0;
  settings.system_showWarning = false;
  settings.disp_showThermalCorePage = settings.disp_showThermalTrackPage = true;
  sim::scenario().setBus(&bus);
  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-core.json", error));

  flightTimer_start();
  populate();
  assert(igcFlight.started());
  const String trackPath = igcFlight.trackLogPath();
  const String entryPath = logbookEntry.path();
  const auto duration = logbook.duration;
  assert(duration >= 340);
  selectPageCursors();
  flightTimer_stop(false);
  assertEmpty();
  checkCursorsCleared();
  assert(!flightTimer_isRunning());
  assert(SD_MMC.exists(trackPath.c_str()) && SD_MMC.exists(entryPath.c_str()));
  File entry = SD_MMC.open(entryPath.c_str());
  JsonDocument json;
  assert(!deserializeJson(json, entry));
  assert(!json["end"].isNull());
  assert(json["metrics"]["duration_seconds"].as<unsigned long>() == duration);
  assert(json["track"]["path"].as<String>() == trackPath);
  entry.close();
  checkNoCachedFix();
  checkAutoSwitchReset();

  flightTimer_start();
  populate();
  checkUnavailableBarometer();

  // Automatic stop uses the same lifecycle boundary as a manual stop.
  flightTimer_start();
  populate();
  extern uint8_t autoStopCounter;
  extern int32_t autoStopAltitude;
  gps.fixInfo.fix = 0;  // No fresh speed is required for the normal no-GPS auto-stop path.
  autoStopCounter = AUTO_STOP_MIN_SEC - 1;
  autoStopAltitude = baro.alt();
  settings.log_autoStop = true;
  log_update();
  assert(!flightTimer_isRunning());
  assertEmpty();
  settings.log_autoStop = false;

  // Exercise active/non-logging sessions, enabled/hidden features and normal/low-battery exits.
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    if (cycle % 2) flightTimer_start();
    populate();
    selectPageCursors();
    settings.disp_showThermalCorePage = settings.disp_showThermalTrackPage = cycle >= 2;
    display.setPage(MainPage::Navigate);
    power.shutdown(cycle >= 2);
    assertEmpty();
    assert(power.info().onState == PowerState::OffUSB);
    assert(power.switchToOnState());
    assert(baro.state() == Barometer::State::WaitingForFirstReading);
    checkCursorsCleared();
    thermalCore.update();
    assertEmpty();
    drawAllPages();
    settings.disp_showThermalCorePage = settings.disp_showThermalTrackPage = true;
    checkNoCachedFix();
  }
  puts("Thermal stop, auto-stop, charging shutdown/wake, stale GPS and barometer readiness passed");
}
