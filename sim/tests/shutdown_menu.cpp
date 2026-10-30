#include <Arduino.h>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "comms/fanet_radio.h"
#include "display_capture.h"
#include "runtime.h"
#include "sim/board.h"
#include "system/shutdown_guard.h"
#include "ui/display/pages/menu/page_menu_developer.h"
#include "ui/settings/settings.h"
#ifndef LEAF_SHUTDOWN_FAULT_INJECTION
#error This test requires the explicit fault-injection build flag.
#endif
class Menu : public DeveloperMenuPage {
 public:
  int cursor() const { return cursor_position; }
  void press(Button button) { button_event(button, ButtonEvent::CLICKED, 0); }
};
void screenshot(Menu& menu, const std::string& path) {
  menu.draw();
  const auto png = sim::encodePng(sim::captureFrame(), 4);
  std::ofstream out(path, std::ios::binary);
  out.write(png.data(), png.size());
  assert(out.good());
}
int main() {
  sim::Runtime::Options options;
  std::filesystem::create_directories("sim/build-tests/shutdown");
  std::string root = "sim/build-tests/shutdown/menu-XXXXXX";
  assert(mkdtemp(root.data()));
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.cardRoot = root + "/card";
  options.statePath = root + "/state";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.dev_mode = true;
  sim::board().fanetPresent = true;
  fanetRadio.setup();
  assert(fanetRadio.isDetected());
  Menu menu;
  for (int i = 0; i < 5; ++i) menu.press(Button::DOWN);
  assert(menu.cursor() == 5);  // Runtime
  menu.press(Button::DOWN);
  assert(menu.cursor() == 6);  // Off fault, immediately after Runtime
  menu.press(Button::RIGHT);
  assert(shutdown_guard::fault() == shutdown_guard::Stage::Lock);
  screenshot(menu, root + "/menu-fanet.png");
  // Like the other Developer selectors, RIGHT/CENTER cycle; LEFT goes back.
  for (int i = 0; i < 7; ++i) menu.press(Button::RIGHT);
  assert(shutdown_guard::fault() == shutdown_guard::Stage::Flight);  // RTC-only option
  screenshot(menu, root + "/menu-rtc.png");
  menu.press(Button::RIGHT);
  assert(shutdown_guard::fault() == shutdown_guard::Stage::None);
  menu.press(Button::CENTER);
  assert(shutdown_guard::fault() == shutdown_guard::Stage::Lock);
  for (int i = 0; i < 8; ++i) menu.press(Button::RIGHT);
  assert(shutdown_guard::fault() == shutdown_guard::Stage::None);
  menu.press(Button::DOWN);
  assert(menu.cursor() == 7);  // Startup options follow the fault row
  menu.press(Button::UP);
  sim::board().fanetPresent = false;
  fanetRadio.setup();
  screenshot(menu, root + "/menu-no-fanet.png");
  menu.press(Button::UP);
  assert(menu.cursor() == 5);
  menu.press(Button::LEFT);
  assert(menu.cursor() == 0);
  std::cout << "Shutdown fault selector order, wrap, and both radio layouts passed; " << root
            << "\n";
}
