/*
 * display.cpp
 *
 *
 */
#include "ui/display/display.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "hardware/Leaf_SPI.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/speaker.h"
#include "ui/display/display_fields.h"
#include "ui/display/display_tests.h"
#include "ui/display/fonts.h"
#include "ui/display/menu_page.h"
#include "ui/display/pages.h"
#include "ui/display/pages/dialogs/page_warning.h"
#include "ui/display/pages/primary/page_charging.h"
#include "ui/display/pages/primary/page_debug.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/display/pages/primary/page_simple.h"
#include "ui/display/pages/primary/page_thermal.h"
#include "ui/display/pages/primary/page_thermal_adv.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

Display display;

#define LCD_BACKLIGHT 21  // can be used for backlight if desired (also broken out to header)
#define LCD_RS 17         // 16 on old V3.2.0
#define LCD_RESET 18      // 17 on old V3.2.0

void GLCD_inst(byte data);
void GLCD_data(byte data);

#ifndef WO256X128  // if not old hardare, use the latest:
U8G2_ST75256_JLX19296_F_4W_HW_SPI u8g2(U8G2_R1,
                                       /* cs=*/SPI_SS_LCD,
                                       /* dc=*/LCD_RS,
                                       /* reset=*/LCD_RESET);
#else  // otherwise use the old hardware settings from v3.2.2:
U8G2_ST75256_WO256X128_F_4W_HW_SPI u8g2(U8G2_R3,
                                        /* cs=*/SPI_SS_LCD,
                                        /* dc=*/LCD_RS,
                                        /* reset=*/LCD_RESET);
#endif

void Display::init(void) {
  {
    // Scope lock as setting a contrast will take its own lock
    SpiLockGuard spiLock;  // Lock the SPI bus before working with it
    pinMode(SPI_SS_LCD, OUTPUT);
    digitalWrite(SPI_SS_LCD, HIGH);
    u8g2.setBusClock(20000000);
    Serial.print("u8g2 set clock. ");
    u8g2.begin();
    Serial.print("u8g2 began. ");

    pinMode(LCD_BACKLIGHT, OUTPUT);
    Serial.println("u8g2 done. ");
  }

  setContrast(settings.disp_contrast);
  Serial.print("u8g2 set contrast. ");
}

void Display::setContrast(uint8_t contrast) {
  SpiLockGuard spiLock;
#ifndef WO256X128  // if not using older hardware, use the latest hardware contrast setting:
  // user can select levels of contrast from 0-20; but display needs values of 115-135.
  u8g2.setContrast(contrast + 115);
#else
  // user can select levels of contrast from 0-20; but display needs values of 182-220.
  u8g2.setContrast(180 + 2 * contrast);
#endif
}

void Display::turnPage(PageAction action) {
  MainPage tempPage = displayPage_;

  switch (action) {
    case PageAction::Home:
      displayPage_ = MainPage::Thermal;
      break;

    case PageAction::Next:
      displayPage_++;

      // skip past any pages not enabled for display
      if (displayPage_ == MainPage::Simple && !settings.disp_showSimplePage) displayPage_++;
      if (displayPage_ == MainPage::Thermal && !settings.disp_showThmPage) displayPage_++;
      if (displayPage_ == MainPage::ThermalAdv && !settings.disp_showThmAdvPage) displayPage_++;
      if (displayPage_ == MainPage::Nav && !settings.disp_showNavPage) displayPage_++;

      break;

    case PageAction::Prev:
      displayPage_--;

      // skip past any pages not enabled for display
      if (displayPage_ == MainPage::Nav && !settings.disp_showNavPage) displayPage_--;
      if (displayPage_ == MainPage::ThermalAdv && !settings.disp_showThmAdvPage) displayPage_--;
      if (displayPage_ == MainPage::Thermal && !settings.disp_showThmPage) displayPage_--;
      if (displayPage_ == MainPage::Simple && !settings.disp_showSimplePage) displayPage_--;
      if (displayPage_ == MainPage::Debug && !settings.disp_showDebugPage)
        displayPage_ = tempPage;  // go back to the page we were on if we can't go further left

      break;

    case PageAction::Back:
      displayPage_ = displayPagePrior_;
  }

  if (displayPage_ != tempPage) displayPagePrior_ = tempPage;
}

void Display::setPage(MainPage targetPage) {
  MainPage tempPage = displayPage_;
  displayPage_ = targetPage;

  if (displayPage_ != tempPage) displayPagePrior_ = tempPage;
}

void Display::showOnSplash() { showSplashScreenFrames_ = 6; }

//*********************************************************************
// MAIN DISPLAY UPDATE FUNCTION
//*********************************************************************
// Will first display charging screen if charging, or splash screen if in the process of turning on
// / waking up Then will display any current modal pages before falling back to the current page
void Display::update() {
  SpiLockGuard spiLock;  // Take out an SPI lock for the rending of the page

  if (displayPage_ == MainPage::Charging) {
    chargingPage_draw();
    return;
  }
  if (showSplashScreenFrames_) {
    display_on_splash();
    showSplashScreenFrames_--;
    return;
  }
  // If user setting to SHOW_WARNING and also we need to showWarning, then display it
  if (settings.system_showWarning && showWarning_) {
    warningPage_draw();
    return;
  } else {
    dismissWarning();
  }

  auto modalPage = mainMenuPage.get_modal_page();
  if (modalPage != NULL) {
    modalPage->draw();
    return;
  }

  switch (displayPage_) {
    case MainPage::Simple:
      simplePage_draw();
      break;
    case MainPage::Thermal:
      thermalPage_draw();
      break;
    case MainPage::ThermalAdv:
      thermalPageAdv_draw();
      break;
    case MainPage::Debug:
      debugPage_draw();
      break;
    case MainPage::Nav:
      navigatePage_draw();
      break;
    case MainPage::Menu:
      mainMenuPage.draw();
      break;
  }
}

void Display::clear() {
  SpiLockGuard spiLock;
  u8g2.clear();
}

void GLCD_inst(byte data) {
  digitalWrite(LCD_RS, LOW);
  spi_writeGLCD(data);
}

void GLCD_data(byte data) {
  digitalWrite(LCD_RS, HIGH);
  spi_writeGLCD(data);
}
