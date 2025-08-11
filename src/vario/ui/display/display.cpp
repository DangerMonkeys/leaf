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
#include "leaf_version.h"
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
#include "ui/display/pages/primary/page_navigate.h"
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

void Display::showOnSplash() { showSplashScreenFrames_ = 3; }

//*********************************************************************
// MAIN DISPLAY UPDATE FUNCTION
//*********************************************************************
// Will first display charging screen if charging, or splash screen if in the process of turning on
// / waking up Then will display any current modal pages before falling back to the current page
void Display::update() {
  SpiLockGuard spiLock;  // Take out an SPI lock for the rending of the page

  if (displayPage_ == MainPage::Charging) {
    showPageCharging();
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
    case MainPage::Thermal:
      thermalPage_draw();
      break;
    case MainPage::ThermalAdv:
      thermalPageAdv_draw();
      break;
    case MainPage::Debug:
      showPageDebug();
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

/*********************************************************************************
**   CHARGING PAGE    ************************************************************
*********************************************************************************/
void Display::showPageCharging() {
  const auto& info = power.info();
  u8g2.firstPage();
  do {
    // Battery Percent
    uint8_t fontOffset = 3;
    if (info.batteryPercent == 100) fontOffset = 0;
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(36 + fontOffset, 12);
    u8g2.print(info.batteryPercent);
    u8g2.print('%');

    display_batt_charging_fullscreen(48, 17);

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(5, 157);
    if (info.inputCurrent == PowerInputLevel::i100mA)
      u8g2.print("100mA");
    else if (info.inputCurrent == PowerInputLevel::i500mA)
      u8g2.print("500mA");
    else if (info.inputCurrent == PowerInputLevel::Max)
      u8g2.print("810mA");
    else if (info.inputCurrent == PowerInputLevel::Standby)
      u8g2.print(" OFF");

    u8g2.print(" ");
    u8g2.print(info.batteryMV);
    u8g2.print("mV");

    // Display the current version
    u8g2.setCursor(0, 172);
    u8g2.setFont(leaf_5x8);
    u8g2.print("v");
    u8g2.print(FIRMWARE_VERSION);

    // SD Card Mounted
    u8g2.setCursor(12, 191);
    u8g2.setFont(leaf_icons);
    if (!sdcard.isMounted()) {
      u8g2.print((char)61);
      u8g2.setFont(leaf_6x12);
      u8g2.print(" NO SD!");
    } else {
      u8g2.print((char)60);
    }

  } while (u8g2.nextPage());
}

/*********************************************************************************
**    DEBUG TEST PAGE     ***************************************************
*********************************************************************************/

void Display::showPageDebug() {
  u8g2.firstPage();
  do {
    // temp display of speed and heading and all that.
    // speed
    u8g2.setCursor(0, 20);
    u8g2.setFont(leaf_6x12);
    u8g2.print(gps.speed.mph(), 1);
    u8g2.setFont(leaf_5h);
    u8g2.drawStr(0, 7, "mph");

    // heading
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(0, 40);
    u8g2.print(gps.course.deg(), 0);
    u8g2.setCursor(30, 40);
    u8g2.print(gps.cardinal(gps.course.deg()));
    u8g2.setFont(leaf_5h);
    u8g2.drawStr(0, 27, "Heading");

    // altitude
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(0, 60);
    u8g2.print(gps.altitude.meters() * 3.028084);
    u8g2.setFont(leaf_5h);
    u8g2.drawStr(0, 47, "alt");

    // Batt Levels
    display_battIcon(89, 13, true);

    uint8_t x = 56;
    uint8_t y = 12;
    const auto& info = power.info();
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(x, y);
    u8g2.print(info.batteryPercent);
    u8g2.print('%');
    u8g2.setCursor(x, y += 6);
    u8g2.setFont(leaf_5h);
    u8g2.print((float)info.batteryMV / 1000, 3);
    u8g2.print("v");

    // Altimeter Setting
    u8g2.setCursor(65, 26);
    u8g2.setFont(leaf_5h);
    u8g2.print("AltSet:");
    u8g2.setCursor(65, 39);
    u8g2.setFont(leaf_6x12);
    u8g2.print(baro.altimeterSetting);

    /*
    // fix quality debugging
    x=0;
    y=76;
    u8g2.setCursor(x,y);
    u8g2.print("FixTyp:");
    u8g2.print(gpsFixInfo.fix);
    u8g2.setCursor(x,y+=13);
    u8g2.print("SatsFix:");
    u8g2.print(gps.satellites.value());
    u8g2.setCursor(x, y+=13);
    u8g2.print("SatsView:");
    u8g2.print(gpsFixInfo.numberOfSats);
    */

    //////////////////////////////////
    // Wind Estimate Debugging
    // get current wind estimate to use in display

    WindEstimate displayEstimate = getWindEstimate();

    // Display Sample Counts
    int numOfBins = getBinCount();
    uint8_t pieX = 48;
    uint8_t pieY = 136;
    uint8_t pieR = 41;
    float binAngle = 2 * PI / numOfBins;

    for (int bin = 0; bin < numOfBins; bin++) {
      float a = binAngle * bin + binAngle / 2;
      int x0 = pieX + sin(a) * (pieR + 5);
      int y0 = pieY - cos(a) * (pieR + 5);

      u8g2.setCursor(x0 - 2, y0 + 4);
      // highlihght the bin that is currently reciving a point
      if (bin == displayEstimate.recentBin) {
        u8g2.drawDisc(x0, y0, 6);
        u8g2.setDrawColor(0);
      }
      u8g2.setFont(leaf_5x8);
      u8g2.print(totalSamples.bin[bin].sampleCount);
      u8g2.setDrawColor(1);
    }

    // coordinate system center lines and axes
    u8g2.drawHLine(pieX - pieR, pieY, pieR * 2);
    u8g2.drawVLine(pieX, pieY - pieR, pieR * 2);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(pieX - 1, pieY - pieR);
    u8g2.print("N");
    u8g2.setCursor(pieX - 1, pieY + pieR + 6);
    u8g2.print("S");
    u8g2.setCursor(pieX - pieR - 6, pieY + 3);
    u8g2.print("W");
    u8g2.setCursor(pieX + pieR + 1, pieY + 3);
    u8g2.print("E");
    u8g2.setFont(leaf_5x8);

    // draw sample points

    // find scale factor to fit the wind estimate on-screen
    float maxGroundSpeed = 1;
    for (int bin = 0; bin < numOfBins; bin++) {
      for (int s = 0; s < totalSamples.bin[bin].sampleCount; s++) {
        if (totalSamples.bin[bin].speed[s] > maxGroundSpeed)
          maxGroundSpeed = totalSamples.bin[bin].speed[s];
      }
    }
    float scaleFactor = pieR / maxGroundSpeed;

    u8g2.setFont(leaf_5h);
    u8g2.setFontMode(1);
    for (int bin = 0; bin < numOfBins; bin++) {
      for (int s = 0; s < totalSamples.bin[bin].sampleCount; s++) {
        int x0 = pieX + totalSamples.bin[bin].dy[s] * scaleFactor;
        int y0 = pieY - totalSamples.bin[bin].dx[s] * scaleFactor;
        u8g2.setCursor(x0 - 2, y0 + 2);
        u8g2.print("+");
      }
    }
    u8g2.setFontMode(0);

    // draw the wind estimate
    uint8_t estX =
        pieX + (sin(displayEstimate.windDirectionTrue) * displayEstimate.windSpeed * scaleFactor);
    uint8_t estY =
        pieY - (cos(displayEstimate.windDirectionTrue) * displayEstimate.windSpeed * scaleFactor);
    uint8_t estR = displayEstimate.airspeed * scaleFactor;

    // circle center point (the wind estimate)
    u8g2.setCursor(estX - 2, estY + 3);
    u8g2.print("&");

    // airspeed circle (the circle fit)
    u8g2.drawCircle(estX, estY, estR);

    // update cycles
    pieX = 5;
    pieY = pieY - pieR - 20;
    u8g2.setCursor(pieX, pieY);
    u8g2.print("upd: ");
    u8g2.setCursor(pieX, pieY += 6);
    u8g2.print(getUpdateCount());
    u8g2.setCursor(pieX, pieY += 8);
    u8g2.print("bet: ");
    u8g2.setCursor(pieX, pieY += 6);
    u8g2.print(getBetterCount());

    x = 48;
    y = 64;
    u8g2.setCursor(x, y);
    u8g2.setFont(leaf_5h);
    u8g2.print("WindEst:");
    u8g2.setCursor(x, y += 9);
    u8g2.setFont(leaf_5x8);
    int16_t windDeg = ((int)(RAD_TO_DEG * displayEstimate.windDirectionTrue + 360)) % 360;
    u8g2.print(windDeg);
    u8g2.print("@");
    u8g2.print(displayEstimate.windSpeed);

    u8g2.setCursor(x = 65, y += 6);
    u8g2.setFont(leaf_5h);
    u8g2.print("EstErr:");
    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(x, y += 9);
    u8g2.print(displayEstimate.error);

    ////////////////////////
    // glide ratio debugging
    /*
    u8g2.setCursor(0, 73);
    u8g2.print("AvCl");
    u8g2.print(baro.climbRateAverage);
    u8g2.setCursor(0, 86);
    u8g2.print("Clmb:");
    u8g2.print(baro.climbRateFiltered);
    u8g2.setCursor(0, 99);
    u8g2.print("GR:");
    u8g2.print(gps_getGlideRatio());

    // time remaining calcs testing
    u8g2.setCursor(0,73);
    u8g2.print("m/s:");
    u8g2.print(gps.speed.mps());
    u8g2.setCursor(0,86);
    u8g2.print("d:");
    u8g2.print(gpxNav.pointDistanceRemaining);
    u8g2.setCursor(0,99);
    u8g2.print("calc:");
    u8g2.print(gpxNav.pointDistanceRemaining / gps.speed.mps());
    */

    //////////////////////////
    // GPS FIX and SATELLITES DEBUGGING
    /*
    u8g2.setCursor(x=52,y=104);
    u8g2.setFont(leaf_5h);
    u8g2.print("TotErr:");
    u8g2.setCursor(x,y+=13);
    u8g2.setFont(leaf_6x12);
    u8g2.print(gpsFixInfo.error);

    u8g2.setCursor(x,y+=6);
    u8g2.setFont(leaf_5h);
    u8g2.print("HDOP:");
    u8g2.setCursor(x,y+=13);
    u8g2.setFont(leaf_6x12);
    u8g2.print(gps.hdop.value());

    u8g2.setCursor(x,y+=6);
    u8g2.setFont(leaf_5h);
    u8g2.print("PosErr:");
    u8g2.setCursor(x,y+=13);
    u8g2.setFont(leaf_6x12);
    u8g2.print(gpsFixInfo.error);

    gpsMenuPage.drawConstellation(0, 106, 63);
    */

  } while (u8g2.nextPage());
}
