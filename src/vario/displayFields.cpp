#include "displayFields.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "SDcard.h"
#include "baro.h"
#include "display.h"
#include "fonts.h"
#include "gps.h"
#include "gpx.h"
#include "log.h"
#include "power.h"
#include "settings.h"
#include "version.h"

/********************************************************************************************/
// Display Components
// Individual fields that can be called from many different pages, and/or placed in different
// positions

void display_selectionBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tri) {
  // first erase necessary pixels to create an internal white border
  u8g2.setDrawColor(0);
  u8g2.drawRFrame(x + 1, y + 1, w - 2, h - 2, 1);
  u8g2.drawTriangle(x + 2, y + h / 2 + tri + 1, x + 2, y + h / 2 - tri - 1, x + 2 + tri, y + h / 2);

  // now draw the box and triangle pointer
  u8g2.setDrawColor(1);
  u8g2.drawRFrame(x, y, w, h, 3);
  u8g2.drawTriangle(x + 1, y + h / 2 + tri, x + 1, y + h / 2 - tri, x + 1 + tri, y + h / 2);
}

void display_clockTime(uint8_t x, uint8_t y, bool show_ampm) {
  int16_t localTimeHHMM = gps_getLocalTimeHHMM();

  u8g2.setCursor(x, y);
  u8g2.setDrawColor(1);

  if (localTimeHHMM <= -1) {  // will be -1 if GPS time is not current/valid
    u8g2.print((char)137);
    u8g2.print("NOGPS");
  } else {  // valid time
    uint8_t hours = localTimeHHMM / 100;
    uint8_t minutes = localTimeHHMM % 100;

    // Serial.print("hours: ");
    // Serial.println(hours);

    bool pm = false;
    if (UNITS_hours) {
      if (hours >= 12) {
        pm = true;
        if (hours > 12) hours -= 12;
      } else if (hours == 0) {
        hours = 12;
      }
    }

    // hours
    if (hours < 10) {
      if (UNITS_hours)
        u8g2.print(' ');  // blank character for 12 hour time
      else
        u8g2.print('0');  // leading 0 for 24-hour time
    } else {
      u8g2.print(hours / 10);
    }
    u8g2.print(hours % 10);

    u8g2.print(':');

    // minutes
    if (minutes < 10) {
      u8g2.print('0');
    } else {
      u8g2.print(minutes / 10);
    }
    u8g2.print(minutes % 10);

    if (UNITS_hours && show_ampm) {
      if (pm)
        u8g2.print("pm");
      else
        u8g2.print("am");
    }
  }
}

void display_waypointTimeRemaining(uint8_t x, uint8_t y, const uint8_t *font) {
  u8g2.setDrawColor(1);
  u8g2.setCursor(x, y);
  u8g2.setFont(font);

  if (gpxNav.pointTimeRemaining == 0) {
    u8g2.print("--:--");
  } else {
    uint8_t sec = gpxNav.pointTimeRemaining % 60;
    uint32_t min = gpxNav.pointTimeRemaining / 60;
    uint8_t hrs = min / 60;
    min = min % 60;  // get rid of any minutes over 60 now that we have the hours

    if (hrs > 9) {
      u8g2.print("9+ hrs");
    } else {
      if (hrs > 0) {
        u8g2.print(hrs);
        u8g2.print(':');
        if (min < 10) {
          u8g2.print('0');
        }
      }
      u8g2.print(min);
      u8g2.print(':');
      if (sec < 10) {
        u8g2.print('0');
      }
      u8g2.print(sec);
    }
  }
}

void display_flightTimer(uint8_t x, uint8_t y, bool shortstring, bool selected) {
  uint8_t h = 16;
  uint8_t w = 44;
  if (shortstring) w = 34;

  u8g2.setDrawColor(1);

  if (!selected) {
    u8g2.drawRBox(x, y - h, w, h, 2);
    u8g2.setDrawColor(0);
  }

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(x + 2, y - 2);
  u8g2.print(flightTimer_getString(shortstring));
  u8g2.setDrawColor(1);

  if (selected) {
    display_selectionBox(x - 1, y - h - 1, w + 2, h + 2, 6);
  }
}

// Display speed (overloaded function allows for with or without font setting and unit character)
void display_speed(uint8_t cursor_x, uint8_t cursor_y) {
  // Get speed in proper units and put into int for display purposes
  uint16_t displaySpeed;
  if (UNITS_speed)
    displaySpeed = gps.speed.mph() +
                   0.5;  // add half so we effectively round when truncating from float to int.
  else
    displaySpeed = gps.speed.kmph() + 0.5;

  if (displaySpeed >= 1000) displaySpeed = 999;  // cap display value at 3 digits

  u8g2.setCursor(cursor_x, cursor_y);
  if (displaySpeed < 10) u8g2.print(" ");   // leave a space if needed
  if (displaySpeed < 100) u8g2.print(" ");  // leave a space if needed
  u8g2.print(displaySpeed, 1);
}
void display_speed(uint8_t x, uint8_t y, const uint8_t *font) {
  u8g2.setFont(font);
  display_speed(x, y);
}
void display_speed(uint8_t x, uint8_t y, const uint8_t *font, bool units) {
  display_speed(x, y, font);
  // kpm or mph
  if (1)
    u8g2.print((char)135);  // unit character
  else
    u8g2.print((char)136);  // unit character
}

// Display distance
void display_distance(uint8_t cursor_x, uint8_t cursor_y, double distance) {
  u8g2.setCursor(cursor_x, cursor_y);
  uint8_t decimalPlaces = 0;
  uint8_t unitsSmall = true;  // assume m or ft, switch to km or mi if needed

  if (UNITS_distance) {
    distance *= 3.28084;  // convert to feet
    if (distance > 1000) {
      distance /= 5280;  // convert to miles if >1000
      unitsSmall = false;
      if (distance < 1000) decimalPlaces = 1;  // show x.1 decimal places if under 1000 miles
      if (distance > 9999) distance = 9999;    // cap max distance
    }
  } else {
    if (distance >= 1000) {
      distance /= 1000;  // switch to km
      unitsSmall = false;
      if (distance > 9999) distance = 9999;
      if (distance < 1000) decimalPlaces = 1;
    }
  }

  if (gpxNav.navigating)
    u8g2.print(distance, decimalPlaces);
  else
    u8g2.print("----");

  if (unitsSmall && UNITS_distance) u8g2.print((char)128);
  if (!unitsSmall && UNITS_distance) u8g2.print((char)130);
  if (unitsSmall && !UNITS_distance) u8g2.print((char)127);
  if (!unitsSmall && !UNITS_distance) u8g2.print((char)129);
}

void display_heading(uint8_t cursor_x, uint8_t cursor_y, bool degSymbol) {
  u8g2.setCursor(cursor_x, cursor_y);

  if (UNITS_heading) {  // Cardinal heading direction
    const char *displayHeadingCardinal =
        gps.cardinal(gps.course.deg());  // gps_getCourseCardinal();
    if (strlen(displayHeadingCardinal) == 1)
      u8g2.setCursor(cursor_x + 8, cursor_y);
    else if (strlen(displayHeadingCardinal) == 2)
      u8g2.setCursor(cursor_x + 4, cursor_y);

    u8g2.print(displayHeadingCardinal);

  } else {  // Degrees heading direction
    uint16_t displayHeadingDegrees = gps.course.deg();
    if (displayHeadingDegrees < 10)
      u8g2.setCursor(cursor_x + 8, cursor_y);
    else if (displayHeadingDegrees < 100)
      u8g2.setCursor(cursor_x + 4, cursor_y);

    u8g2.print(displayHeadingDegrees);
    if (degSymbol) u8g2.print('*');
  }
}

uint8_t turnThreshold1 = 20;
uint8_t turnThreshold2 = 40;
uint8_t turnThreshold3 = 60;

void display_headingTurn(uint8_t cursor_x, uint8_t cursor_y) {
  u8g2.setCursor(cursor_x, cursor_y);
  u8g2.setFont(leaf_7x10);

  double offCourse = gpxNav.turnToActive;
  int8_t turn = 0;

  if (offCourse > turnThreshold1) {
    if (offCourse > turnThreshold3)
      turn = 3;
    else if (offCourse > turnThreshold2)
      turn = 2;
    else
      turn = 1;
  } else if (offCourse < -turnThreshold1) {
    if (offCourse < -turnThreshold3)
      turn = -3;
    else if (offCourse < -turnThreshold2)
      turn = -2;
    else
      turn = -1;
  }

  // Left turn arrow if needed
  char displayTurn = '=' + turn;  // in the 7x10 font, '=' is the "no turn" center state; 3 chars to
                                  // each side are incresing amounts of turn arrow
  if (displayTurn < '=') u8g2.print(displayTurn);

  display_heading(cursor_x + 8, cursor_y, false);

  // Right turn arrow if needed
  u8g2.setCursor(cursor_x + 32, cursor_y);
  if (displayTurn > '=') u8g2.print(displayTurn);
}

void display_alt_type(uint8_t cursor_x, uint8_t cursor_y, const uint8_t *font, uint8_t altType) {
  int32_t displayAlt = 0;

  switch (altType) {
    case altType_MSL:
      displayAlt = baro.altAdjusted;
      break;
    case altType_AGL:
      break;
    case altType_GPS:
      displayAlt = 100 * gps.altitude.meters();  // gps returns float in m, convert to int32_t in cm
      break;
    case altType_aboveLaunch:
      displayAlt = baro.altAboveLaunch;
      break;
    case altType_aboveWaypoint:
      displayAlt = gpxNav.altAboveWaypoint;
      break;
    case altType_aboveGoal:
      break;
    case altType_aboveLZ:
      break;
  }
  display_alt(cursor_x, cursor_y, font, displayAlt);
}

void display_alt(uint8_t cursor_x, uint8_t cursor_y, const uint8_t *font, int32_t displayAlt) {
  if (UNITS_alt)
    displayAlt = displayAlt * 100 / 3048;  // convert cm to ft
  else
    displayAlt /= 100;  // convert from cm to m

  u8g2.setCursor(cursor_x, cursor_y);
  u8g2.setFont(font);

  bool negativeVal = false;

  if (displayAlt < 0) {
    negativeVal = true;
    displayAlt *= -1;
    if (displayAlt > 9999) displayAlt = 9999;  // max size if negative
  } else if (displayAlt > 99999)
    displayAlt = 99999;  // max size if positive

  uint8_t digits = 0;
  bool keepZeros = 0;

  // Thousands piece
  if (displayAlt > 999) {
    digits = displayAlt / 1000;
    displayAlt -= (1000 * digits);  // save the last 3 digits for later
    keepZeros = 1;  // and keep leading zeros for rest of digits since we printed something in
                    // thousands place
    if (digits < 10) {
      if (negativeVal)
        u8g2.print('-');  // negative sign for negative values (negative values are capped at 9999
                          // so 'digits < 10' will always be true)
      else
        u8g2.print(" ");  // otherwise leading space as needed for positive values
    }
    u8g2.print(digits);
    u8g2.print(',');
  }
  // rest of the number
  if (keepZeros) {
    for (int i = 100; i > 0; i /= 10) {
      digits = displayAlt / i;
      displayAlt -= (digits * i);
      u8g2.print(digits);
    }
  } else {             // don't show leading zeros for altitudes less than 1000
    u8g2.print("  ");  // print two spaces for the thousands places
    if (displayAlt < 100) u8g2.print(" ");
    if (displayAlt < 10) u8g2.print(" ");
    u8g2.print(displayAlt);
  }
}

void print_alt_label(uint8_t altType) {
  u8g2.setFont(leaf_labels);

  switch (altType) {
    case altType_MSL:
      u8g2.print("MSL");
      break;
    case altType_AGL:
      break;
    case altType_GPS:
      u8g2.print("GPS");
      break;
    case altType_aboveLaunch:
      u8g2.print("ALH");
      break;
    case altType_aboveLZ:
      u8g2.print("ALZ");
      break;
    case altType_aboveWaypoint:
      u8g2.print("AWP");
      break;
  }
}

void display_varioBar(uint8_t varioBarFrame_top,
                      uint8_t varioBarFrame_length,
                      uint8_t varioBarFrame_width,
                      int32_t displayBarClimbRate) {
  int16_t varioBar_climbRateMax = 500;   // this is the bar height, but because we can fill then
                                         // empty the bar, we can show twice this climb value
  int16_t varioBar_climbRateMin = -500;  // again, bar height, so we can show twice this sink value

  uint8_t varioBarFrame_mid = varioBarFrame_top + varioBarFrame_length / 2;

  u8g2.drawFrame(0, varioBarFrame_top, varioBarFrame_width, varioBarFrame_length);

  // u8g2.setDrawColor(0);
  // u8g2.drawBox(1, varioBarFrame_top+1, varioBarFrame_length-2, varioBarFrame_width-2);    //only
  // needed if varioBar overlaps something else u8g2.setDrawColor(1);

  // Fill varioBar
  uint8_t varioBarFill_top = varioBarFrame_top + 1;
  uint8_t varioBarFill_bot = varioBarFrame_top + varioBarFrame_length - 2;
  uint8_t varioBarFill_top_length = varioBarFrame_mid - varioBarFill_top + 1;
  uint8_t varioBarFill_bot_length = varioBarFill_bot - varioBarFrame_mid + 1;

  int16_t varioBarFill_pixels = 0;
  uint8_t varioBarFill_start = 1;
  uint8_t varioBarFill_end = 1;

  if (displayBarClimbRate > 2 * varioBar_climbRateMax ||
      displayBarClimbRate < 2 * varioBar_climbRateMin) {
    // do nothing, the bar is maxxed out which looks empty

  } else if (displayBarClimbRate > varioBar_climbRateMax) {
    // fill top half inverted
    varioBarFill_pixels = varioBarFill_top_length * (displayBarClimbRate - varioBar_climbRateMax) /
                          varioBar_climbRateMax;
    varioBarFill_start = varioBarFill_top;
    varioBarFill_end = varioBarFrame_mid - varioBarFill_pixels;

  } else if (displayBarClimbRate < varioBar_climbRateMin) {
    // fill bottom half inverted
    varioBarFill_pixels = varioBarFill_bot_length * (displayBarClimbRate - varioBar_climbRateMin) /
                          varioBar_climbRateMin;
    varioBarFill_start = varioBarFrame_mid + varioBarFill_pixels;
    varioBarFill_end = varioBarFill_bot;

  } else if (displayBarClimbRate < 0) {
    // fill bottom half positive
    varioBarFill_pixels = varioBarFill_bot_length * (displayBarClimbRate) / varioBar_climbRateMin;
    varioBarFill_start = varioBarFrame_mid;
    varioBarFill_end = varioBarFrame_mid + varioBarFill_pixels;

  } else {
    // fill top half positive
    varioBarFill_pixels = varioBarFill_top_length * (displayBarClimbRate) / varioBar_climbRateMax;
    varioBarFill_start = varioBarFrame_mid - varioBarFill_pixels;
    varioBarFill_end = varioBarFrame_mid;
  }

  u8g2.drawBox(
      1, varioBarFill_start, varioBarFrame_width - 2, varioBarFill_end - varioBarFill_start + 1);

  // Tick marks on varioBar
  uint8_t tickSpacing = varioBarFill_top_length / 5;  // start with top half tick spacing
  uint8_t line_y = varioBarFrame_top;

  for (int i = 1; i <= 9; i++) {
    if (i == 5) {
      // at midpoint, switch to bottom half
      line_y = varioBarFrame_mid;
      tickSpacing = varioBarFill_bot_length / 5;
    } else {
      // draw a tick-mark line
      line_y += tickSpacing;
      if (line_y >= varioBarFill_start && line_y <= varioBarFill_end) {
        u8g2.setDrawColor(0);
      } else {
        u8g2.setDrawColor(1);
      }
      u8g2.drawLine(1, line_y, varioBarFrame_width / 2 - 1, line_y);
    }
  }
}

void display_climbRatePointerBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t triSize) {
  u8g2.setDrawColor(1);
  u8g2.drawBox(x, y, w, h);
  u8g2.drawTriangle(
      x - triSize, y + (h) / 2, x - 1, y + (h) / 2 - triSize, x - 1, y + (h) / 2 + triSize);
  u8g2.setDrawColor(0);
  u8g2.drawLine(x - triSize - 1, y + (h) / 2, x - 2, y + (h) / 2 - triSize + 1);
  u8g2.drawLine(x - triSize - 1, y + (h) / 2, x - 2, y + (h) / 2 + triSize - 1);
}

void display_climbRate(uint8_t x, uint8_t y, const uint8_t *font, int16_t displayClimbRate) {
  u8g2.setCursor(x, y);
  u8g2.setFont(font);
  u8g2.setDrawColor(0);

  float climbInMS = 0;

  if (displayClimbRate >= 0)
    u8g2.print('+');
  else {
    u8g2.print('-');
    displayClimbRate *= -1;  // keep positive part
  }

  if (UNITS_climb) {
    displayClimbRate = displayClimbRate * 197 / 1000 *
                       10;  // convert from cm/s to fpm (lose one significant digit)
    if (displayClimbRate < 1000) u8g2.print(" ");
    if (displayClimbRate < 100) u8g2.print(" ");
    if (displayClimbRate < 10) u8g2.print(" ");
    u8g2.print(displayClimbRate);
    /*
    if (font == leaf_21h) {
      u8g2.print("f");
    } else {
      u8g2.setFont(leaf_labels);
      u8g2.print(" fpm");
    }
    */
  } else {
    displayClimbRate =
        (displayClimbRate + 5) / 10;  // lose one decimal place and round off in the process
    climbInMS = (float)displayClimbRate /
                10;  // convert to float for ease of printing with the decimal in place
    if (climbInMS < 10) u8g2.print(" ");
    u8g2.print(climbInMS, 1);
    /*
    if (font == leaf_21h) {
      u8g2.print("m");
    } else {
      u8g2.setFont(leaf_labels);
      u8g2.print(" m/s");
    }
    */
  }
  u8g2.setDrawColor(1);  // always set back to 1 if we've been using 0, just in case the next draw
                         // function forgets to set to 1
}

void display_altAboveLaunch(uint8_t x, uint8_t y, int32_t aboveLaunchAlt) {
  u8g2.setCursor(x, y - 16);
  u8g2.setFont(leaf_5h);
  u8g2.print("ABOVE LAUNCH");
  display_alt(x, y, leaf_8x14, aboveLaunchAlt);
}

void display_altAboveLaunch(uint8_t x, uint8_t y, int32_t aboveLaunchAlt, const uint8_t *font) {
  u8g2.setFont(font);
  uint8_t h = u8g2.getMaxCharHeight();
  u8g2.setCursor(x, y - h - 2);
  u8g2.setFont(leaf_5h);
  u8g2.print("ABOVE LAUNCH");
  display_alt(x, y, font, aboveLaunchAlt);
}

void display_accel(uint8_t x, uint8_t y, float accel) {
  u8g2.setCursor(x, y);
  u8g2.setDrawColor(1);
  u8g2.setFont(leaf_6x12);
  u8g2.print(accel, 1);
  u8g2.print(" g");
}

void display_glide(uint8_t x, uint8_t y, float glide) {
  u8g2.setDrawColor(1);
  u8g2.setFont(leaf_6x12);

  // the 'usual' positive glide angle (going down)
  if (glide > 0) {
    if (glide < 10) x += 7;
    if (glide >= 100) glide = 99.9;  // clip at max display glide
    u8g2.setCursor(x, y);
    u8g2.print(glide, 1);
  } else {  // 0 or negative glide ratio (this means the angle is 'upward', since traditionally a
            // positive glide angle is still 'going down')
    u8g2.setCursor(x, y);
    u8g2.print("--.-");
  }
}

void display_temp(uint8_t x, uint8_t y, int16_t temperature) {
  u8g2.setCursor(x, y);
  u8g2.setDrawColor(1);
  u8g2.setFont(leaf_6x12);

  if (UNITS_temp) {
    temperature = temperature * 9 / 5 + 32;
    u8g2.print(temperature);
    u8g2.print((char)134);
  } else {
    u8g2.print(temperature);
    u8g2.print((char)133);
  }
}

void display_humidity(uint8_t x, uint8_t y, uint8_t humidity) {
  u8g2.setCursor(x, y);
  u8g2.setDrawColor(1);
  u8g2.setFont(leaf_6x12);

  u8g2.print(humidity);
  u8g2.print('%');
}

void display_battIcon(uint8_t x, uint8_t y, bool vertical) {
  /*
      / batt charging (not full)
      0 batt at 0%
      1 batt at 10%
      --etc--
      9 batt at 90%
      : batt at 100%
      ; batt charging (full)
  */

  uint8_t battPercent = power_getBattLevel(0);
  // uint16_t battMV = power_getBattLevel(1);
  // uint16_t battADC = power_getBattLevel(2);

  char battIcon = '0' + (battPercent + 5) / 10;

  if (power_getBattCharging()) {
    if (battPercent >= 100)
      battIcon = ';';
    else
      battIcon = '/';
  }

  u8g2.setCursor(x, y);
  if (vertical)
    u8g2.setFont(Leaf_BattIcon_Vertical);
  else
    u8g2.setFont(leaf_icons);
  u8g2.print(battIcon);
  /*
  Serial.print("percent: ");
  Serial.print(battPercent);
  Serial.print(" icon: ");
  Serial.print((char)battIcon);
  Serial.print(" milivolts: ");
  Serial.print(battMV);
  Serial.print(" ADC: ");
  Serial.println(battADC);

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(x, y+=15);
  u8g2.print(battPercent);
  u8g2.setCursor(x, y+=15);
  u8g2.print(battMV);
  u8g2.setCursor(x, y+=15);
  u8g2.print(battADC);
  */
}

uint8_t batt_percent_testing = 0;
uint8_t batt_charging_testing = 1;
int8_t batt_testing_direction = 1;

void display_batt_charging_fullscreen(uint8_t x, uint8_t y) {
  // size of battery
  uint8_t w = 60;   // 60;
  uint8_t h = 120;  // 140;
  uint8_t t = 2;    // wall thickness

  // Battery Canister
  u8g2.setDrawColor(1);
  u8g2.drawRBox(x - w / 5, y - 1, w / 5 * 2, w / 3, w / 22);  // battery tip nub
  u8g2.drawRBox(x - (w / 2), y + w / 20, w, h, w / 15);       // main rectangle outline
  u8g2.setDrawColor(0);
  u8g2.drawRBox(x - (w / 2) + t,
                y + w / 20 + t,
                w - 2 * t,
                h - 2 * t,
                w / 15 - t);  // empty internal volume

  // Battery Capacity Fill
  uint8_t battPercent = power_getBattLevel(0);

  /*
  battPercent = batt_percent_testing;
  batt_percent_testing += batt_testing_direction;
  if (batt_percent_testing >= 100) {
    batt_testing_direction = -1;
    batt_charging_testing = 0;
  } else if (batt_percent_testing == 0) {
    batt_testing_direction = 1;
    batt_charging_testing = 1;
  }
  */

  uint8_t fill_h = (h - 4 * t) * battPercent / 100;
  uint8_t fill_y = (y + w / 20 + 2 * t) +
                   ((h - 4 * t) - fill_h);  // (starting position to allow for line thickness etc) +
                                            // ( (100% height) - (actual height) )
  u8g2.setDrawColor(1);
  u8g2.drawBox(x - (w / 2) + 2 * t, fill_y, w - 4 * t, fill_h);  //, w/8-t/2);

  // Charging bolt
  if (power_getBattCharging()) {     // batt_charging_testing) {
    uint8_t bolt_x1 = w * 6 / 100;   //  4
    uint8_t bolt_x2 = w * 7 / 100;   //  6
    uint8_t bolt_x3 = w * 21 / 100;  // 17
    uint8_t bolt_y1 = h * 3 / 100;   //  2
    uint8_t bolt_y2 = h * 25 / 100;  // 19
    uint8_t bolt_y = y + h / 2;      // center of bolt in y direction

    u8g2.setDrawColor(0);
    u8g2.drawTriangle(x + bolt_x1,
                      bolt_y + bolt_y1,
                      x + bolt_x2,
                      bolt_y - bolt_y2,
                      x - bolt_x3,
                      bolt_y + bolt_y1);
    u8g2.drawTriangle(x - bolt_x1,
                      bolt_y - bolt_y1,
                      x - bolt_x2,
                      bolt_y + bolt_y2,
                      x + bolt_x3,
                      bolt_y - bolt_y1);

    for (int i = 0; i < 4; i++) {
      if (i == 0) {
        x--;
      }  // drag the bolt outline around to make it thicker
      if (i == 1) {
        bolt_y++;
      }  //
      if (i == 2) {
        bolt_y--;
        x++;
      }  //
      if (i == 3) {
        bolt_y++;
      }  //

      u8g2.setDrawColor(1);
      u8g2.drawLine(x - bolt_x3, bolt_y + bolt_y1, x + bolt_x2, bolt_y - bolt_y2);
      u8g2.drawLine(x + bolt_x1, bolt_y - bolt_y1, x + bolt_x2, bolt_y - bolt_y2);
      u8g2.drawLine(x + bolt_x1, bolt_y - bolt_y1, x + bolt_x3, bolt_y - bolt_y1);
      u8g2.drawLine(x - bolt_x2, bolt_y + bolt_y2, x + bolt_x3, bolt_y - bolt_y1);
      u8g2.drawLine(x - bolt_x2, bolt_y + bolt_y2, x - bolt_x1, bolt_y + bolt_y1);
      u8g2.drawLine(x - bolt_x3, bolt_y + bolt_y1, x - bolt_x1, bolt_y + bolt_y1);
    }
  }
}

// Wind Sock Triangle
void display_windSock(int16_t x, int16_t y, int16_t radius, float wind_angle) {
  // int16_t wind_triangle_radius = 10;
  // if (wind_angle > 2 * PI) wind_angle = 0;

  int16_t wind_triangle_tip_len = radius - 1;   // pixels from center for point
  int16_t wind_triangle_tail_len = radius - 1;  // pixels from center for the tails
  float wind_triangle_tail_angle = 0.65;        // radians tail spread (half-angle)

  // u8g2.setDrawColor(0);
  // u8g2.drawDisc(x, y, radius);
  // u8g2.setDrawColor(1);
  // u8g2.drawCircle(x, y, radius);
  // u8g2.drawTriangle(32, 96, 36, 106, 28, 106);

  uint16_t tip_xprime = x + sin(wind_angle + PI) * wind_triangle_tip_len;
  uint16_t tip_yprime = y - cos(wind_angle + PI) * wind_triangle_tip_len;
  uint16_t tail_1_xprime = x + sin(wind_angle + wind_triangle_tail_angle) * wind_triangle_tail_len;
  uint16_t tail_1_yprime = y - cos(wind_angle + wind_triangle_tail_angle) * wind_triangle_tail_len;
  uint16_t tail_2_xprime = x + sin(wind_angle - wind_triangle_tail_angle) * wind_triangle_tail_len;
  uint16_t tail_2_yprime = y - cos(wind_angle - wind_triangle_tail_angle) * wind_triangle_tail_len;
  uint16_t tail_mid_xprime = x + sin(wind_angle) * wind_triangle_tail_len / 2;
  uint16_t tail_mid_yprime = y - cos(wind_angle) * wind_triangle_tail_len / 2;

  u8g2.drawTriangle(
      tip_xprime, tip_yprime, tail_1_xprime, tail_1_yprime, tail_mid_xprime, tail_mid_yprime);
  u8g2.drawTriangle(
      tip_xprime, tip_yprime, tail_2_xprime, tail_2_yprime, tail_mid_xprime, tail_mid_yprime);
  u8g2.drawLine(tip_xprime, tip_yprime, tail_1_xprime, tail_1_yprime);
  u8g2.drawLine(tail_mid_xprime, tail_mid_yprime, tail_2_xprime, tail_2_yprime);
  u8g2.drawLine(tip_xprime, tip_yprime, tail_2_xprime, tail_2_yprime);
  u8g2.drawLine(tail_mid_xprime, tail_mid_yprime, tail_1_xprime, tail_1_yprime);
}

// END OF COMPONENT DISPLAY FUNCTIONS //
/********************************************************************************************/

// Full Screen Display Functions

// Header and Footer Items to show on ALL pages
void display_headerAndFooter(bool headingShowTurn, bool timerSelected) {
  // Status Icons and Info ****************************************************
  // clock time
  u8g2.setFont(leaf_6x10);
  display_clockTime(0, 10, false);

  // battery
  display_battIcon(0, 192, true);

  // SD Card Present
  char SDicon = 60;
  if (!SDcard_present()) SDicon = 61;
  u8g2.setCursor(10, 192);
  u8g2.setFont(leaf_icons);
  u8g2.print((char)SDicon);

  // Heading in top center
  uint8_t heading_x = 38;
  uint8_t heading_y = 10;
  u8g2.setFont(leaf_7x10);
  if (headingShowTurn)
    display_headingTurn(heading_x, heading_y);
  else
    display_heading(heading_x + 8, heading_y, true);

  // Speed in upper right corner
  u8g2.setFont(leaf_8x14);
  display_speed(70, 14);
  u8g2.setFont(leaf_5h);
  u8g2.setCursor(82, 21);
  if (UNITS_speed)
    u8g2.print("MPH");
  else
    u8g2.print("KPH");

  // Timer in lower right corner
  display_flightTimer(51, 191, 0, timerSelected);
}

void display_splashLogo() { u8g2.drawXBM(0, 20, 96, 123, splash_logo_bmp); }

void display_off_splash() {
  u8g2.firstPage();
  do {
    display_splashLogo();

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(20, 180);
    u8g2.print("GOODBYE");
  } while (u8g2.nextPage());
}

void display_on_splash() {
  u8g2.firstPage();
  do {
    display_splashLogo();

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(28, 170);
    u8g2.print("HELLO");

    u8g2.setCursor(17, 192);
    u8g2.print("VER: ");
    u8g2.print(VERSION);
  } while (u8g2.nextPage());
}
