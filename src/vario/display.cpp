/*
 * display.cpp
 *
 *
 */
#include "display.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "Leaf_SPI.h"
#include "PageNavigate.h"
#include "PageThermal.h"
#include "PageThermalSimple.h"
#include "SDcard.h"
#include "baro.h"
#include "displayFields.h"
#include "display_tests.h"
#include "fonts.h"
#include "gps.h"
#include "gpx.h"
#include "log.h"
#include "menu_page.h"
#include "pages.h"
#include "power.h"
#include "settings.h"
#include "speaker.h"
#include "version.h"

// Display Testing Temp Vars
float wind_angle = 1.57;
char seconds = 0;
char minutes = 0;
char hours = 0;
uint16_t heading = 0;
char string_heading[] = " WNW ";

// Leaf Hardware V3.2.2+
U8G2_ST75256_JLX19296_F_4W_HW_SPI u8g2(U8G2_R1,
                                       /* cs=*/SPI_SS_LCD,
                                       /* dc=*/LCD_RS,
                                       /* reset=*/LCD_RESET);

int8_t display_page = page_thermalSimple;
uint8_t display_page_prior =
    page_thermalSimple;  // track the page we used to be on, so we can "go back" if needed (like
                         // cancelling out of a menu heirarchy)

void display_init(void) {
  pinMode(SPI_SS_LCD, OUTPUT);
  digitalWrite(SPI_SS_LCD, HIGH);
  u8g2.setBusClock(20000000);
  Serial.print("u8g2 set clock. ");
  u8g2.begin();
  Serial.print("u8g2 began. ");
  display_setContrast(CONTRAST);
  Serial.print("u8g2 set contrast. ");

  pinMode(LCD_BACKLIGHT, OUTPUT);
  Serial.println("u8g2 done. ");
}

void display_setContrast(uint8_t contrast) {
  u8g2.setContrast(
      contrast +
      115);  // user can select levels of contrast from 0-20; but display needs values of 115-135.
}

void display_turnPage(uint8_t action) {
  uint8_t tempPage = display_page;

  switch (action) {
    case page_home:
      display_page = page_thermalSimple;
      break;

    case page_next:
      display_page++;

      // skip past any pages not enabled for display
      if (display_page == page_thermalSimple && !SHOW_THRM_SIMP) display_page++;
      if (display_page == page_thermal && !SHOW_THRM_ADV) display_page++;
      if (display_page == page_nav && !SHOW_NAV) display_page++;

      if (display_page == page_last)
        display_page =
            0;  // bound check if we fall off the right side, wrap around to the right side
      break;

    case page_prev:
      display_page--;

      // skip past any pages not enabled for display
      if (display_page == page_nav && !SHOW_NAV) display_page--;
      if (display_page == page_thermal && !SHOW_THRM_ADV) display_page--;
      if (display_page == page_thermalSimple && !SHOW_THRM_SIMP) display_page--;
      if (display_page == page_debug && !SHOW_DEBUG)
        display_page = tempPage;  // go back to the page we were on if we can't go further left

      if (display_page < 0)
        display_page = page_last - 1;  // bound check if we fall off the left side -- wrap around to
                                       // the last page (usually the menu page)
      break;

    case page_back:
      display_page = display_page_prior;
  }

  if (display_page != tempPage) display_page_prior = tempPage;
}

void display_setPage(uint8_t targetPage) {
  uint8_t tempPage = display_page;
  display_page = targetPage;

  if (display_page != tempPage) display_page_prior = tempPage;
}

uint8_t display_getPage() { return display_page; }

uint8_t showSplashScreenFrames = 0;

void display_showOnSplash() { showSplashScreenFrames = 3; }

//*********************************************************************
// MAIN DISPLAY UPDATE FUNCTION
//*********************************************************************
// Will first display charging screen if charging, or splash screen if in the process of turning on
// / waking up Then will display any current modal pages before falling back to the current page
void display_update() {
  if (display_page == page_charging) {
    display_page_charging();
    return;
  }
  if (showSplashScreenFrames) {
    display_on_splash();
    showSplashScreenFrames--;
    return;
  }

  auto modalPage = mainMenuPage.get_modal_page();
  if (modalPage != NULL) {
    modalPage->draw();
    return;
  }

  switch (display_page) {
    case page_thermalSimple:
      thermalSimplePage_draw();
      break;
    case page_thermal:
      thermalPage_draw();
      break;
    case page_debug:
      display_page_debug();
      break;
    case page_nav:
      navigatePage_draw();
      break;
    case page_menu:
      mainMenuPage.draw();
      break;
  }
}

void display_clear() { u8g2.clear(); }

void GLCD_inst(byte data) {
  digitalWrite(LCD_RS, LOW);
  spi_writeGLCD(data);
}

void GLCD_data(byte data) {
  digitalWrite(LCD_RS, HIGH);
  spi_writeGLCD(data);
}

char speed[] = "132";
char windSpeed[] = "28";
char turn = 1;
uint16_t windDir = 235;
int32_t varioBar_climbRate = -100;  // cm/s  (i.e. m/s * 100)
int8_t climbChange = 10;

char altitude[] = "23,857\"";
char altAbvLaunch[] = "4,169";
char glide[] = "10.4";
char distFlown[] = "34.5";
char glideToWypt[] = " 6.5";
char timeToWypt[] = "12:34";
char distToWypt[] = "42.7";
char waypoint[] = "Marshall-LZ";
char temp[] = "102";
char accel[] = "2.1g";
char altAbvLZ[] = "1,987";
char climbRate[] = "+1385";
char clockTime[] = "12:57pm";
char timer[] = "1:23:45";
float dirToWypt = -.25;

/*

float gps_getSpeed_kph() { return gps.speed.kmph(); }
float gps_getSpeed_mph() { return gps.speed.mph(); }
float gps_getCourseDeg() { return gps.course.deg(); }
float gps_getAltMeters() { return gps.altitude.meters(); }

*/

void display_update_temp_vars() {
  dirToWypt += .005;
  wind_angle -= .0075;

  varioBar_climbRate += climbChange;
  if (varioBar_climbRate > 1100) {
    climbChange *= -1;
    varioBar_climbRate = 1090;
  } else if (varioBar_climbRate < -1100) {
    climbChange *= -1;
    varioBar_climbRate = -1090;
  }
}

/*********************************************************************************
**   CHARGING PAGE    ************************************************************
*********************************************************************************/
void display_page_charging() {
  u8g2.firstPage();
  do {
    uint16_t battMV = power_getBattLevel(1);
    uint16_t battPercent = power_getBattLevel(0);

    // Battery Percent
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(34, 12);
    u8g2.print(battPercent);
    u8g2.print('%');

    display_batt_charging_fullscreen(48, 17);

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(5, 157);
    if (power_getInputCurrent() == i100mA)
      u8g2.print("100mA");
    else if (power_getInputCurrent() == i500mA)
      u8g2.print("500mA");
    else if (power_getInputCurrent() == iMax)
      u8g2.print("810mA");
    else if (power_getInputCurrent() == iStandby)
      u8g2.print(" OFF");

    u8g2.print(" ");
    u8g2.print(battMV);
    u8g2.print("mV");

    // Display the current version
    u8g2.setCursor(35, 172);
    u8g2.setFont(leaf_5x8);
    u8g2.print("v");
    u8g2.print(VERSION);

    // SD Card Present
    u8g2.setCursor(12, 191);
    u8g2.setFont(leaf_icons);
    if (!SDcard_present()) {
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

void display_page_debug() {
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

    uint8_t battPercent = power_getBattLevel(0);
    uint16_t battMV = power_getBattLevel(1);
    uint16_t battADC = power_getBattLevel(2);
    uint8_t x = 64;
    uint8_t y = 25;
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(x, y);
    u8g2.print(battPercent);
    u8g2.print('%');

    u8g2.setCursor(x, y += 15);
    u8g2.print(battMV);
    u8g2.setCursor(x, y += 15);
    u8g2.print(battADC);

    // glide ratio debugging
    u8g2.setCursor(0, 73);
    u8g2.print("AvCl");
    u8g2.print(baro.climbRateAverage);
    u8g2.setCursor(0, 86);
    u8g2.print("Clmb:");
    u8g2.print(baro.climbRateFiltered);
    u8g2.setCursor(0, 99);
    u8g2.print("GR:");
    u8g2.print(gps_getGlideRatio());

    /*
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

    u8g2.setCursor(65, 110);
    u8g2.print(baro.altimeterSetting);

    gpsMenuPage.drawConstellation(0, 100, 63);

  } while (u8g2.nextPage());
}
