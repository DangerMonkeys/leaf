#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <assert.h>
#include <stdio.h>

#include <cmath>
#include <fstream>

#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "navigation/gpx.h"
#include "navigation/route_store.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/settings/settings.h"

extern MessageBus<11> bus;

namespace {
  class TestModal : public MenuPage {
   public:
    bool button_event(Button, ButtonEvent, uint8_t) override { return false; }
    void draw() override {}
    void open() { push_page(this); }
  };
  void saveScreen(const char* path) {
    const auto png = sim::encodePng(sim::captureFrame(), 4);
    std::ofstream out(path, std::ios::binary);
    out.write(png.data(), png.size());
    assert(out.good());
  }

  void nav(Button button) { navigatePage_button(button, ButtonEvent::CLICKED, 0); }

  void gpsSentence(const char* body) {
    unsigned checksum = 0;
    for (const char* p = body; *p; ++p) checksum ^= *p;
    char sentence[96];
    snprintf(sentence, sizeof(sentence), "$%s*%02X", body, checksum);
    gps.on_receive(GpsMessage(NMEAString(sentence)));
  }

  void checkFieldWidths() {
    u8g2.firstPage();
    do {
      for (bool imperial : {false, true}) {
        settings.units_distance = imperial;
        for (double distance : {0., 100., 999., 9999., 99999., 999900., 1609000., 9999000.}) {
          u8g2.setFont(leaf_6x12);
          display_distance(57, 154, distance, 38);
          assert(u8g2.getCursorX() <= 95);
        }
      }
      for (uint32_t seconds : {0u, 3599u, 3600u, 35999u, 36000u, 999999u}) {
        display_navTimeRemaining(57, 154, seconds >= 3600 ? leaf_5x8 : leaf_6x12, seconds);
        assert(u8g2.getCursorX() <= 95);
      }
      u8g2.setFont(leaf_5h);
      assert(u8g2.getStrWidth("TIME>GOAL") <= 43);
      assert(u8g2.getStrWidth("DIST>GOAL") <= 43);
      assert(u8g2_GetGlyphWidth(u8g2.getU8g2(), '^') > 0);
      assert(u8g2.getStrWidth("ALT^&") <= 43);
      assert(u8g2.getStrWidth("ALT^GOAL") <= 43);
      for (bool feet : {false, true}) {
        settings.units_alt = feet;
        for (float altitude : {0.f, 12.3f, 999.f, 9999.f, 99999.f, 1.e8f, -1.f, NAN, INFINITY}) {
          display_navAltitudeSurplus(52, 154, altitude);
          assert(u8g2.getCursorX() <= 95);
        }
      }
      u8g2.setFont(leaf_5x8);
      assert(u8g2.getStrWidth("Auto Switch?") <= 73);
    } while (u8g2.nextPage());
  }

  void cycleField(uint8_t field) {
    const auto before = settings.disp_navFields[field];
    nav(Button::CENTER);
    assert(settings.disp_navFields[field] != before);
  }

  void checkNavCursor() {
    settings.disp_showNavPage = true;
    display.setPage(MainPage::Navigate);
    nav(Button::UP);  // timer
    nav(Button::UP);  // field 4
    cycleField(3);
    navigatePage_draw();
    saveScreen("sim/build-tests/nav-field4.png");
    nav(Button::UP);  // field 2
    cycleField(1);
    nav(Button::LEFT);  // field 1
    cycleField(0);
    nav(Button::LEFT);  // wraps to field 2
    cycleField(1);
    nav(Button::RIGHT);  // wraps to field 1
    nav(Button::DOWN);   // field 3
    cycleField(2);
    navigatePage_draw();
    saveScreen("sim/build-tests/nav-field3.png");
    nav(Button::RIGHT);  // field 4
    cycleField(3);
    nav(Button::LEFT);  // field 3
    nav(Button::UP);    // field 1
    nav(Button::UP);    // waypoint
    nav(Button::UP);    // altitude
    const auto altitude = settings.disp_navPageAltType;
    nav(Button::CENTER);
    assert(settings.disp_navPageAltType != altitude);
    nav(Button::DOWN);  // waypoint
    nav(Button::DOWN);  // field 2
    cycleField(1);
    navigatePage_draw();
    saveScreen("sim/build-tests/nav-field2.png");
    nav(Button::LEFT);  // field 1
    navigatePage_draw();
    saveScreen("sim/build-tests/nav-field1.png");
    // Every choice can be reached and wraps, independently of other fields.
    const auto first = settings.disp_navFields[0];
    for (int i = 0; i < static_cast<int>(NavField::Count); ++i) cycleField(0);
    assert(settings.disp_navFields[0] == first);
    nav(Button::DOWN);  // field 3
    nav(Button::DOWN);  // timer
    nav(Button::DOWN);  // no cursor
  }

  void checkAutoSwitch() {
    settings.disp_showThermalCorePage = true;
    settings.autoSwitchToThermalCorePage = true;
    display.setPage(MainPage::Navigate);
    display.updateThermalAutoSwitch(false);
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::ThermalCore);
    display.updateThermalAutoSwitch(false);
    assert(display.getPage() == MainPage::Navigate);

    display.updateThermalAutoSwitch(true);
    display.turnPage(PageAction::Prev);  // pilot overrides automatic visit
    assert(display.getPage() == MainPage::User);
    display.updateThermalAutoSwitch(true);
    display.updateThermalAutoSwitch(false);
    assert(display.getPage() == MainPage::User);  // never restore Nav after manual override
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::ThermalCore);
    display.updateThermalAutoSwitch(false);
    assert(display.getPage() == MainPage::User);

    display.setPage(MainPage::Menu);
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::Menu);
    display.setPage(MainPage::Navigate);
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::Navigate);  // event in menu does not steal focus later
    display.updateThermalAutoSwitch(false);
    TestModal modal;
    modal.open();
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::Navigate);
    modal.pop_all_pages();
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::Navigate);
    display.updateThermalAutoSwitch(false);
    display.updateThermalAutoSwitch(true);
    assert(display.getPage() == MainPage::ThermalCore);
    display.setPage(MainPage::Menu);  // menu entry also cancels restoration
    display.updateThermalAutoSwitch(false);
    assert(display.getPage() == MainPage::Menu);

    display.setPage(MainPage::ThermalCore);  // manually selected Core has no automatic return
    display.updateThermalAutoSwitch(true);
    display.updateThermalAutoSwitch(false);
    assert(display.getPage() == MainPage::ThermalCore);
    settings.disp_showThermalCorePage = false;
    display.updateThermalAutoSwitch(true);
    assert(!settings.autoSwitchToThermalCorePage);
    settings.disp_showThermalCorePage = true;
    display.setPage(MainPage::Navigate);
  }

  void checkDisplayMenu() {
    settings.disp_showThermalCorePage = false;
    settings.autoSwitchToThermalCorePage = false;
    displayMenuPage.resetCursor();
    for (int i = 0; i < 3; ++i) displayMenuPage.button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
    displayMenuPage.draw();
    saveScreen("sim/build-tests/display-core-off.png");
    displayMenuPage.button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
    assert(settings.disp_showThermalCorePage);
    displayMenuPage.button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
    displayMenuPage.button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
    assert(settings.autoSwitchToThermalCorePage);
    displayMenuPage.draw();
    saveScreen("sim/build-tests/display-auto-switch.png");
    displayMenuPage.button_event(Button::UP, ButtonEvent::CLICKED, 0);
    displayMenuPage.button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
    assert(!settings.disp_showThermalCorePage && !settings.autoSwitchToThermalCorePage);
    // Hidden Auto Switch row must be skipped; next row controls Track.
    const bool track = settings.disp_showThermalTrackPage;
    displayMenuPage.button_event(Button::DOWN, ButtonEvent::CLICKED, 0);
    displayMenuPage.button_event(Button::CENTER, ButtonEvent::CLICKED, 0);
    assert(settings.disp_showThermalTrackPage != track);
  }

  void checkPersistence() {
    settings.disp_showThermalCorePage = true;
    settings.disp_showThermalTrackPage = true;
    settings.autoSwitchToThermalCorePage = true;
    const NavField fields[] = {NavField::TimeToGoal, NavField::DistanceToWaypoint,
                               NavField::AltitudeAboveWaypoint, NavField::AltitudeAboveGoal};
    for (uint8_t i = 0; i < 4; ++i) settings.disp_navFields[i] = fields[i];
    settings.save();
    settings.loadDefaults();
    settings.retrieve();
    for (uint8_t i = 0; i < 4; ++i) assert(settings.disp_navFields[i] == fields[i]);
    assert(settings.disp_showThermalCorePage && settings.disp_showThermalTrackPage &&
           settings.autoSwitchToThermalCorePage);

    Preferences prefs;
    prefs.begin("varioPrefs");
    prefs.putUChar("NAVPG_USR1", 255);
    prefs.remove("NAVPG_USR2");
    prefs.remove("THERM_DISPLAY");
    prefs.putBool("LAB_THERM_CORE", false);
    prefs.putBool("LAB_THERM_TRACK", true);
    prefs.end();
    settings.retrieve();
    assert(settings.disp_navFields[0] == DEFAULT_NAV_FIELDS[0]);
    assert(settings.disp_navFields[1] == DEFAULT_NAV_FIELDS[1]);
    assert(!settings.disp_showThermalCorePage && !settings.autoSwitchToThermalCorePage);
    assert(settings.disp_showThermalTrackPage);
    settings.save();
    settings.disp_showThermalCorePage = true;  // Display now controls it without Labs
    settings.autoSwitchToThermalCorePage = true;
    settings.save();
    settings.retrieve();
    assert(settings.disp_showThermalCorePage && settings.autoSwitchToThermalCorePage);
  }

  void checkGoalMetrics() {
    assert(gps.hasUsableFix() && gps.hasFreshGroundSpeed());
    navigator.clear();
    const double lat = gps.location.lat(), lon = gps.location.lng();
    for (int i = 0; i < 4; ++i) {
      Waypoint point;
      point.setName("Test");
      point.setCoordinates(lat + (i == 0 ? -0.1 : i == 3 ? 0.01 : 0.1), lon + (i == 2 ? 0.1 : 0));
      point.ele = gps.altitude.meters() - (i == 3 ? 200 : 100);
      assert(navigator.addWaypoint(point));
    }
    navigator.totalRoutes = 1;
    auto& route = navigator.routes[1];
    route = Route();
    route.setName("Dogleg");
    for (int i = 1; i <= 4; ++i) assert(navigator.addRoutePoint(&route, WaypointID(i), 100));
    assert(navigator.activateRoute(RouteID(1), RouteIndex(2), false));
    navigator.update();
    const auto distance = [](const Waypoint& a, const Waypoint& b) {
      return gps.distanceBetween(a.latitude(), a.longitude(), b.latitude(), b.longitude());
    };
    const double expected = navigator.pointDistanceRemaining +
                            distance(navigator.waypoints[2], navigator.waypoints[3]) +
                            distance(navigator.waypoints[3], navigator.waypoints[4]);
    assert(std::abs(navigator.goalDistanceRemaining() - expected) < 0.01);
    assert(expected > 10 * gps.distanceBetween(lat, lon, navigator.waypoints[4].latitude(),
                                               navigator.waypoints[4].longitude()));
    assert(std::abs(navigator.glideToGoal() - expected / 200) < 0.01);
    assert(navigator.goalTimeRemaining() ==
           static_cast<uint32_t>((expected - 100) / gps.speed.mps()));
    const double glide = gps.getGlideRatio();
    assert(glide > 0 && baro.climbRateFilteredValid());
    navigator.activePoint.ele =
        gps.altitude.meters() - navigator.pointDistanceRemaining / glide - 100;
    navigator.waypoints[4].ele = gps.altitude.meters() - expected / glide - 200;
    navigator.update();
    assert(std::abs(navigator.arrivalAltitudeAboveWaypoint() - 100) < 0.01);
    assert(std::abs(navigator.arrivalAltitudeAboveGoal() - 200) < 0.01);
    // A direct-to-goal shortcut would report a much larger surplus on this dogleg.
    const double shortcutSurplus = gps.altitude.meters() - navigator.waypoints[4].ele -
                                   gps.distanceBetween(lat, lon, navigator.waypoints[4].latitude(),
                                                       navigator.waypoints[4].longitude()) /
                                       glide;
    assert(shortcutSurplus > navigator.arrivalAltitudeAboveGoal() + 100);
    settings.disp_navFields[0] = NavField::AltitudeAboveWaypoint;
    settings.disp_navFields[1] = NavField::AltitudeAboveGoal;
    settings.disp_navFields[2] = NavField::DistanceToGoal;
    settings.disp_navFields[3] = NavField::CurrentGlide;
    for (bool feet : {false, true}) {
      settings.units_alt = feet;
      navigatePage_draw();
      saveScreen(feet ? "sim/build-tests/nav-altitude-surplus-feet.png"
                      : "sim/build-tests/nav-altitude-surplus-metres.png");
    }
    const auto waypointElevation = navigator.activePoint.ele;
    const auto goalElevation = navigator.waypoints[4].ele;
    navigator.activePoint.ele = NAN;
    assert(std::isnan(navigator.arrivalAltitudeAboveWaypoint()));
    navigator.activePoint.ele = waypointElevation;
    navigator.waypoints[4].ele = NAN;
    navigator.update();
    assert(std::isnan(navigator.arrivalAltitudeAboveGoal()));
    navigator.waypoints[4].ele = gps.altitude.meters() - expected / glide + 1;
    navigator.update();
    assert(std::isnan(navigator.arrivalAltitudeAboveGoal()));
    navigatePage_draw();
    saveScreen("sim/build-tests/nav-altitude-surplus-unavailable.png");
    navigator.waypoints[4].ele = goalElevation;
    navigator.activePoint.ele =
        gps.altitude.meters() - navigator.pointDistanceRemaining / glide + 1;
    assert(std::isnan(navigator.arrivalAltitudeAboveWaypoint()));
    navigator.activePoint.ele = waypointElevation;
    navigator.waypoints[4].ele = gps.altitude.meters() - 200;
    settings.disp_navFields[0] = NavField::TimeToGoal;
    settings.disp_navFields[1] = NavField::DistanceToGoal;
    settings.disp_navFields[2] = NavField::GlideToGoal;
    navigatePage_draw();
    saveScreen("sim/build-tests/nav-goal-fields.png");
    assert(navigator.activateRoute(RouteID(1), RouteIndex(4), false));
    navigator.update();
    assert(std::abs(navigator.goalDistanceRemaining() - navigator.pointDistanceRemaining) < 0.01);
    assert(navigator.goalTimeRemaining() == navigator.pointTimeRemaining);
    navigator.waypoints[4].ele = gps.altitude.meters() + 100;
    assert(navigator.activateRoute(RouteID(1), RouteIndex(4), false));
    navigator.update();
    assert(navigator.glideToGoal() == 0);  // no descending glide solution to a higher goal
    navigator.waypoints[4].ele = gps.altitude.meters() - 200;
    assert(navigator.activatePoint(WaypointID(4), false));
    navigator.update();
    assert(std::abs(navigator.glideToGoal() - navigator.glideToActive) < 0.01);
    for (uint8_t averaging : {0, 20}) {
      gps.setGlideAverageSeconds(averaging);
      gps.update();
      assert(gps.getGlideRatio() > 0);
      const double surplus = gps.altitude.meters() - navigator.activePoint.ele -
                             navigator.pointDistanceRemaining / gps.getGlideRatio();
      assert(std::abs(navigator.arrivalAltitudeAboveWaypoint() - surplus) < 0.01);
    }
    gps.setGlideAverageSeconds(settings.glideAverageSeconds);
    gps.update();
    // An exactly representable altitude makes the zero-surplus boundary deterministic.
    gpsSentence("GNGGA,130030,4730.0000,N,01100.0000,E,1,08,0.9,1000.0,M,0,M,,");
    gps.update();
    navigator.activePoint.ele = 0;  // A known sea-level height is valid.
    navigator.pointDistanceRemaining = 0;
    assert(std::abs(navigator.arrivalAltitudeAboveWaypoint() - gps.altitude.meters()) < 0.01);
    navigator.activePoint.ele = gps.altitude.meters();
    assert(navigator.arrivalAltitudeAboveWaypoint() == 0);
    navigator.activePoint.ele = -50;  // Negative terrain elevation is also valid.
    assert(navigator.arrivalAltitudeAboveWaypoint() > gps.altitude.meters());
    gps.fixInfo.fix = 0;
    assert(std::isnan(navigator.arrivalAltitudeAboveWaypoint()));
    gps.fixInfo.fix = 1;
    sim::clock().advanceUs(4000000);
    assert(std::isnan(navigator.arrivalAltitudeAboveWaypoint()));
    gps.update();
    assert(gps.getGlideRatio() == 0);
    navigator.cancelNav();
    assert(std::isnan(navigator.arrivalAltitudeAboveWaypoint()));
    assert(std::isnan(navigator.arrivalAltitudeAboveGoal()));
    assert(navigator.goalDistanceRemaining() == 0 && navigator.goalTimeRemaining() == 0 &&
           navigator.glideToGoal() == 0);
  }

  void checkElevationImports() {
    const char* path = "/arrival-elevation-test.gpx";
    File file = SD_MMC.open(path, "w", true);
    assert(file);
    file.print(R"(<gpx>
      <wpt lat="47" lon="11"><name>Unknown</name></wpt>
      <wpt lat="47.1" lon="11"><name>Sea level</name><ele>0</ele></wpt>
      <wpt lat="47.2" lon="11"><name>Below sea</name><ele>-50</ele></wpt>
      <wpt lat="47.3" lon="11"><name>Bad height</name><ele>invalid</ele></wpt>
    </gpx>)");
    file.close();
    navigator.clear();
    assert(gpx_readFile(SD_MMC, path));
    assert(std::isnan(navigator.waypoints[1].ele));
    assert(navigator.waypoints[2].ele == 0);
    assert(navigator.waypoints[3].ele == -50);
    assert(std::isnan(navigator.waypoints[4].ele));
    SD_MMC.remove(path);

    route_store::ImportResult result;
    assert(route_store::importRouteText("Arrival elevation test", R"({"taskType":"CLASSIC",
      "turnpoints":[
        {"waypoint":{"name":"Unknown","lat":47,"lon":11}},
        {"waypoint":{"name":"Sea level","lat":47.1,"lon":11,"altSmoothed":0}}
      ]})",
                                        false, result));
    assert(route_store::loadRouteFile(result.path, false));
    assert(std::isnan(navigator.waypoints[1].ele));
    assert(navigator.waypoints[2].ele == 0);
    SD_MMC.remove(result.path);
  }
}  // namespace

int main(int argc, char**) {
  auto& device = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/display-settings/state";
  options.cardRoot = "sim/build-tests/display-settings/card";
  device.configure(options);
  device.boot();
  if (argc > 1) {
    assert(settings.disp_navFields[0] == DEFAULT_NAV_FIELDS[0]);
    assert(settings.disp_navFields[1] == DEFAULT_NAV_FIELDS[1]);
    assert(settings.disp_navFields[2] == NavField::AltitudeAboveWaypoint);
    assert(settings.disp_navFields[3] == NavField::AltitudeAboveGoal);
    assert(settings.disp_showThermalCorePage && settings.disp_showThermalTrackPage &&
           settings.autoSwitchToThermalCorePage);
    puts("Display settings survived a fresh simulator process boot");
    return 0;
  }
  settings.loadDefaults();
  sim::scenario().setBus(&bus);
  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-core.json", error));
  sim::scenario().play();
  while (sim::clock().nowUs() < 30000000) device.step();
  sim::scenario().pause();
  checkElevationImports();
  checkGoalMetrics();
  checkNavCursor();
  checkAutoSwitch();
  checkDisplayMenu();
  checkFieldWidths();
  checkPersistence();
  settings.autoSwitchToThermalCorePage = true;
  display.setPage(MainPage::User);
  display.updateThermalAutoSwitch(false);
  sim::scenario().play();
  bool sawAutomaticVisit = false;
  while (sim::clock().nowUs() < 350000000) {
    device.step();
    sawAutomaticVisit |= display.getPage() == MainPage::ThermalCore;
  }
  assert(sawAutomaticVisit && display.getPage() == MainPage::User);
  settings.save();
  puts(
      "Nav fields, route goal metrics, display menu, persistence and thermal auto-switch checks "
      "passed");
}
