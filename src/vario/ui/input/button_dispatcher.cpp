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
#include "ui/display/pages/primary/page_charging.h"
#include "ui/display/pages/primary/page_debug.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/display/pages/primary/page_thermal.h"
#include "ui/display/pages/primary/page_thermal_adv.h"
#include "ui/settings/settings.h"

ButtonDispatcher buttonDispatcher;

void ButtonDispatcher::on_receive(const ButtonEventMessage& msg) {
  power.resetAutoOffCounter();  // pressing any button should reset the auto-off counter
                                // TODO: we should probably have a counter for Auto-Timer-Off as
                                // well, and button presses should reset that.
  MainPage currentPage = display.getPage();  // actions depend on which page we're on

  if (currentPage == MainPage::Charging) {
    chargingPage_button(msg.button, msg.event, msg.holdCount);
    return;
  }
  if (display.displayingWarning()) {
    warningPage_button(msg.button, msg.event, msg.holdCount);
    display.update();
    return;
  }

  // If there's a modal page currently shown, we should send the button event to that page
  auto modal_page = mainMenuPage.get_modal_page();
  if (modal_page != NULL) {
    bool draw_now = modal_page->button_event(msg.button, msg.event, msg.holdCount);
    if (draw_now) display.update();
    return;
  }

  if (currentPage == MainPage::Menu) {
    bool draw_now = mainMenuPage.button_event(msg.button, msg.event, msg.holdCount);
    if (draw_now) display.update();

  } else if (currentPage == MainPage::Thermal) {
    thermalPage_button(msg.button, msg.event, msg.holdCount);

  } else if (currentPage == MainPage::ThermalAdv) {
    thermalPageAdv_button(msg.button, msg.event, msg.holdCount);

  } else if (currentPage == MainPage::Nav) {
    navigatePage_button(msg.button, msg.event, msg.holdCount);

  } else if (currentPage == MainPage::Debug) {  // NOT CHARGING PAGE (i.e., our debug test page)
    debugPage_button(msg.button, msg.event, msg.holdCount);
  }
}
