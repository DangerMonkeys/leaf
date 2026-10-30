#include "ui/display/pages/menu/page_menu_developer.h"

#include <Arduino.h>

#include "comms/fanet_radio.h"
#include "diagnostics/cpu_utilization.h"
#include "diagnostics/self_test/selfTest.h"
#include "hardware/buttons.h"
#include "logging/buslog.h"
#include "logging/fanet_log.h"
#include "system/shutdown_guard.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum developer_menu_items {
  cursor_developer_back,
  cursor_developer_fanetReTx,
  cursor_developer_logFanet,
  cursor_developer_showDebugPg,
  cursor_developer_runSelfTest,
  cursor_developer_runtime,
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
  cursor_developer_shutdownFault,
#endif
  cursor_developer_startupStart,
  cursor_developer_startupDisconnect,
  cursor_developer_busLogControl
};
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
namespace {
  uint8_t offFaultIndex = 0;
  const shutdown_guard::Stage offFaultStages[] = {
      shutdown_guard::Stage::None,      shutdown_guard::Stage::Lock,
      shutdown_guard::Stage::Flight,    shutdown_guard::Stage::BleWorker,
      shutdown_guard::Stage::Sound,     shutdown_guard::Stage::Peripherals,
      shutdown_guard::Stage::BleDeinit, shutdown_guard::Stage::Flight,
      shutdown_guard::Stage::Flight};
  const char* offFaultLabels[] = {"OFF", "LOCK", "SAVE", "WORK", "BEEP",
                                  "I2C", "BLE",  "WDT",  "RTC"};
}  // namespace
#endif

bool DeveloperMenuPage::row_hidden(uint8_t row) const {
  return (row == cursor_developer_fanetReTx || row == cursor_developer_logFanet) &&
         !fanetRadio.isDetected();
}

bool DeveloperMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  const bool redraw = SettingsMenuPage::button_event(button, state, count);
  while (row_hidden(cursor_position)) {
    if (button == Button::UP)
      cursor_prev();
    else
      cursor_next();
  }
  return redraw;
}

void DeveloperMenuPage::draw() {
  if (row_hidden(cursor_position)) cursor_position = cursor_developer_back;
  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Developer", menu_ui::GLYPH_DEVELOPER);

    // Menu Items
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 76;
    uint8_t menu_items_y[10] = {190};
    uint8_t next_y = 35;
    for (int i = cursor_developer_fanetReTx; i <= cursor_developer_runtime; ++i) {
      if (row_hidden(i)) continue;
      menu_items_y[i] = next_y;
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
      next_y += 13;
#else
      next_y += 15;
#endif
    }
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
    menu_items_y[cursor_developer_shutdownFault] = next_y;
    next_y += 13;
#endif
    menu_items_y[cursor_developer_startupStart] = next_y + 25;
    menu_items_y[cursor_developer_startupDisconnect] = next_y + 40;
    menu_items_y[cursor_developer_busLogControl] = next_y + 60;

    // draw Bus Logger Section
    u8g2.drawHLine(0, next_y - 5, 96);
    u8g2.setCursor(0, next_y + 10);
    u8g2.print("On Startup:");

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      if (row_hidden(i)) continue;
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
        case cursor_developer_shutdownFault:
          if (shutdown_guard::fault() == shutdown_guard::Stage::None) offFaultIndex = 0;
          u8g2.setCursor(66, menu_items_y[i]);
          u8g2.print(offFaultLabels[offFaultIndex]);
          break;
#endif
        case cursor_developer_fanetReTx:
          if (settings.dev_fanetFwd)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_developer_showDebugPg:
          if (settings.disp_showDebugPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_developer_logFanet:
          menu_ui::printGlyph(settings.dev_logFanet ? menu_ui::ICON_ON : menu_ui::ICON_OFF);
          break;
        case cursor_developer_runSelfTest:
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_developer_runtime:
          u8g2.setCursor(u8g2.getCursorX() - 18, u8g2.getCursorY());
          u8g2.print(cpu_utilization::capturing()                                  ? "STOP"
                     : cpu_utilization::hasCapture()                               ? "SAVE"
                     : strcmp(cpu_utilization::status(), "allocation_failed") == 0 ? "ERR"
                                                                                   : "START");
          break;
        case cursor_developer_startupStart:
          if (settings.dev_startLogAtBoot)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_developer_startupDisconnect:
          if (settings.dev_startDisconnected)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_developer_busLogControl:
          u8g2.setCursor(u8g2.getCursorX() - 18, u8g2.getCursorY());
          if (busLog.isLogging()) {
            u8g2.print("STOP");
          } else {
            u8g2.print("START");
          }
          break;
        case cursor_developer_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void DeveloperMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (row_hidden(cursor_position)) return;
  switch (cursor_position) {
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
    case cursor_developer_shutdownFault:
      if (state == ButtonEvent::CLICKED) {
        offFaultIndex = (offFaultIndex + (dir == Button::LEFT ? 8 : 1)) % 9;
        shutdown_guard::setFault(offFaultStages[offFaultIndex],
                                 offFaultIndex == 7   ? shutdown_guard::FaultBehavior::InterruptsOff
                                 : offFaultIndex == 8 ? shutdown_guard::FaultBehavior::RtcOnly
                                                      : shutdown_guard::FaultBehavior::Stall);
      }
      break;
#endif
    case cursor_developer_runtime:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        if (cpu_utilization::capturing()) {
          settings.diag_cpuUtilization = false;
          cpu_utilization::freeze();
          speaker.playSound(cpu_utilization::exportToSd() ? fx::confirm : fx::bad);
        } else if (cpu_utilization::hasCapture()) {
          settings.diag_cpuUtilization = false;
          speaker.playSound(cpu_utilization::exportToSd() ? fx::confirm : fx::bad);
        } else {
          const bool started = cpu_utilization::start();
          settings.diag_cpuUtilization = started;
          if (!started) speaker.playSound(fx::bad);
        }
      }
      break;
    case cursor_developer_logFanet:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        settings.toggleBoolOnOff(&settings.dev_logFanet);
        if (!fanet_log::update()) speaker.playSound(fx::bad);
      }
      break;
    case cursor_developer_fanetReTx: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        settings.toggleBoolOnOff(&settings.dev_fanetFwd);
      break;
    }
    case cursor_developer_showDebugPg: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        settings.toggleBoolOnOff(&settings.disp_showDebugPage);
      break;
    }
    case cursor_developer_runSelfTest: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        cursor_position = cursor_developer_back;
        selfTest.begin(false);  // start a self test (not the official production test)
      }
      break;
    }
    case cursor_developer_startupStart: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        settings.toggleBoolOnOff(&settings.dev_startLogAtBoot);
      break;
    }
    case cursor_developer_startupDisconnect: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        settings.toggleBoolOnOff(&settings.dev_startDisconnected);
      break;
    }
    case cursor_developer_busLogControl: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        if (!busLog.isLogging()) {
          if (busLog.startLog()) {
            speaker.playSound(fx::started);
          } else {
            speaker.playSound(fx::bad);
          }
        } else {
          busLog.endLog();
        }
      }
      break;
    }
    case cursor_developer_back: {
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
    }
    default:
      break;
  }
}
