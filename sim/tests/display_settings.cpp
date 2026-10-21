#include <Arduino.h>
#include <Preferences.h>
#include <assert.h>
#include <stdio.h>

#include <cmath>
#include <fstream>

#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "instruments/gps.h"
#include "navigation/gpx.h"
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
    for (uint8_t i = 0; i < 4; ++i) settings.disp_navFields[i] = static_cast<NavField>(i + 1);
    settings.save();
    settings.loadDefaults();
    settings.retrieve();
    for (uint8_t i = 0; i < 4; ++i)
      assert(settings.disp_navFields[i] == static_cast<NavField>(i + 1));
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
    navigator.cancelNav();
    assert(navigator.goalDistanceRemaining() == 0 && navigator.goalTimeRemaining() == 0 &&
           navigator.glideToGoal() == 0);
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
    assert(settings.disp_navFields[2] == NavField::DistanceToGoal);
    assert(settings.disp_navFields[3] == NavField::CurrentGlide);
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
