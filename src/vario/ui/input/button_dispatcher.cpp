#include "ui/input/button_dispatcher.h"

#include <Arduino.h>

#include "hardware/buttons.h"
#include "instruments/baro.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/menu_page.h"
#include "ui/display/pages.h"
#include "ui/display/pages/dialogs/page_warning.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/display/pages/primary/page_thermal.h"
#include "ui/display/pages/primary/page_thermal_adv.h"
#include "ui/settings/settings.h"

ButtonDispatcher buttonDispatcher;

void ButtonDispatcher::on_receive(const ButtonEvent& msg) {
  power.resetAutoOffCounter();  // pressing any button should reset the auto-off counter
                                // TODO: we should probably have a counter for Auto-Timer-Off as
                                // well, and button presses should reset that.
  MainPage currentPage = display.getPage();  // actions depend on which page we're on

  if (currentPage == MainPage::Charging) {
    switch (msg.button) {
      case Button::CENTER:
        if (msg.state == HELD && msg.holdCount == 1) {
          display.clear();
          display.showOnSplash();
          display.setPage(
              MainPage::Thermal);  // TODO: set initial page to the user's last used page
          speaker.playSound(fx::enter);
          buttons.lockAfterHold();  // lock buttons until user lets go of power button
          power.switchToOnState();
        }
        break;
      case Button::UP:
        switch (msg.state) {
          case RELEASED:
            break;
          case HELD:
            power.increaseInputCurrent();

            speaker.playSound(fx::enter);
            break;
        }
        break;
      case Button::DOWN:
        switch (msg.state) {
          case RELEASED:
            break;
          case HELD:
            power.decreaseInputCurrent();
            speaker.playSound(fx::exit);
            break;
        }
        break;
    }
    return;
  }
  if (display.displayingWarning()) {
    warningPage_button(msg.button, msg.state, msg.holdCount);
    display.update();
    return;
  }

  // If there's a modal page currently shown, we should send the button event to that page
  auto modal_page = mainMenuPage.get_modal_page();
  if (modal_page != NULL) {
    bool draw_now = modal_page->button_event(msg.button, msg.state, msg.holdCount);
    if (draw_now) display.update();
    return;
  }

  if (currentPage == MainPage::Menu) {
    bool draw_now = mainMenuPage.button_event(msg.button, msg.state, msg.holdCount);
    if (draw_now) display.update();

  } else if (currentPage == MainPage::Thermal) {
    thermalPage_button(msg.button, msg.state, msg.holdCount);
    display.update();

  } else if (currentPage == MainPage::ThermalAdv) {
    thermalPageAdv_button(msg.button, msg.state, msg.holdCount);
    display.update();

  } else if (currentPage == MainPage::Nav) {
    navigatePage_button(msg.button, msg.state, msg.holdCount);
    display.update();

  } else if (currentPage != MainPage::Charging) {  // NOT CHARGING PAGE (i.e., our debug test page)
    switch (msg.button) {
      case Button::CENTER:
        switch (msg.state) {
          case HELD:
            if (msg.holdCount == 2) {
              power.shutdown();
              while (buttons.inspectPins() == Button::CENTER) {
              }  // freeze here until user lets go of power button
              display.setPage(MainPage::Charging);
            }
            break;
          case RELEASED:
            display.turnPage(PageAction::Home);
            break;
        }
        break;
      case Button::RIGHT:
        if (msg.state == RELEASED) {
          display.turnPage(PageAction::Next);
          speaker.playSound(fx::increase);
        }
        break;
      case Button::LEFT:
        /* Don't allow turning page further to the left
        if (msg.state == RELEASED) {
          display_turnPage(page_prev);
          speaker_playSound(fx::decrease);
        }
        */
        break;
      case Button::UP:
        switch (msg.state) {
          case RELEASED:
            baro.adjustAltSetting(1, 0);
            break;
          case HELD:
            baro.adjustAltSetting(1, 1);
            break;
          case HELD_LONG:
            baro.adjustAltSetting(1, 10);
            break;
        }
        break;
      case Button::DOWN:
        switch (msg.state) {
          case RELEASED:
            baro.adjustAltSetting(-1, 0);
            break;
          case HELD:
            baro.adjustAltSetting(-1, 1);
            break;
          case HELD_LONG:
            baro.adjustAltSetting(-1, 10);
            break;
        }
        break;
    }
  }
}
