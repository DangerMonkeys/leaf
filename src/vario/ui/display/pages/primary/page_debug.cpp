#include <Arduino.h>
#include <U8g2lib.h>

#include "hardware/buttons.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "navigation/nav_ids.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

/*********************************************************************************
**    DEBUG TEST PAGE     ***************************************************
*********************************************************************************/

void debugPage_draw() {
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

    const WindEstimate& displayEstimate = windEstimator.getWindEstimate();

    // Display Sample Counts
    int numOfBins = windEstimator.binCount();
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
      u8g2.print(windEstimator.totalSamples().bin[bin].sampleCount);
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
      for (int s = 0; s < windEstimator.totalSamples().bin[bin].sampleCount; s++) {
        if (windEstimator.totalSamples().bin[bin].speed[s] > maxGroundSpeed)
          maxGroundSpeed = windEstimator.totalSamples().bin[bin].speed[s];
      }
    }
    float scaleFactor = pieR / maxGroundSpeed;

    u8g2.setFont(leaf_5h);
    u8g2.setFontMode(1);
    for (int bin = 0; bin < numOfBins; bin++) {
      for (int s = 0; s < windEstimator.totalSamples().bin[bin].sampleCount; s++) {
        int x0 = pieX + windEstimator.totalSamples().bin[bin].dy[s] * scaleFactor;
        int y0 = pieY - windEstimator.totalSamples().bin[bin].dx[s] * scaleFactor;
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
    u8g2.print(windEstimator.updateCount());
    u8g2.setCursor(pieX, pieY += 8);
    u8g2.print("bet: ");
    u8g2.setCursor(pieX, pieY += 6);
    u8g2.print(windEstimator.betterCount());

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

  } while (u8g2.nextPage());
}

void debugPage_button(Button button, ButtonEvent state, uint8_t count) {
  switch (button) {
    case Button::CENTER:
      switch (state) {
        case ButtonEvent::INCREMENTED:
          if (count == 2) {
            power.shutdown();
            while (buttons.inspectPins() == Button::CENTER) {
            }  // freeze here until user lets go of power button
            display.setPage(MainPage::Charging);
          }
          break;
        case ButtonEvent::CLICKED:
          display.turnPage(PageAction::Home);
          break;
      }
      break;
    case Button::RIGHT:
      if (state == ButtonEvent::CLICKED) {
        display.turnPage(PageAction::Next);
        speaker.playSound(fx::increase);
      }
      break;
    case Button::LEFT:
      /* Don't allow turning page further to the left
      if (msg.event == ButtonState::CLICKED) {
        display_turnPage(page_prev);
        speaker_playSound(fx::decrease);
      }
      */
      break;
    case Button::UP:
      switch (state) {
        case ButtonEvent::CLICKED:
          baro.adjustAltSetting(1, 0);
          break;
        case ButtonEvent::HELD:
          baro.adjustAltSetting(1, 1);
          break;
        case ButtonEvent::HELD_LONG:
          baro.adjustAltSetting(1, 10);
          break;
      }
      break;
    case Button::DOWN:
      switch (state) {
        case ButtonEvent::CLICKED:
          baro.adjustAltSetting(-1, 0);
          break;
        case ButtonEvent::HELD:
          baro.adjustAltSetting(-1, 1);
          break;
        case ButtonEvent::HELD_LONG:
          baro.adjustAltSetting(-1, 10);
          break;
      }
      break;
  }
  display.update();
}