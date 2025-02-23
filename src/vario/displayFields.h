#ifndef displayFields_h
#define displayFields_h

#include <Arduino.h>

void display_selectionBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tri);

void display_clockTime(uint8_t x, uint8_t y, bool show_ampm);
void display_flightTimer(uint8_t x, uint8_t y, bool shortstring, bool selected);
void display_waypointTimeRemaining(uint8_t x, uint8_t y, const uint8_t* font);
void display_speed(uint8_t cursor_x, uint8_t cursor_y);
void display_speed(uint8_t x, uint8_t y, const uint8_t* font);
void display_speed(uint8_t x, uint8_t y, const uint8_t* font, bool units);
void display_distance(uint8_t cursor_x, uint8_t cursor_y, double distance);
void display_heading(uint8_t cursor_x, uint8_t cursor_y, bool degSymbol);
void display_headingTurn(uint8_t cursor_x, uint8_t cursor_y);

void display_alt(uint8_t cursor_x, uint8_t cursor_y, const uint8_t* font, int32_t displayAlt);
void display_altAboveLaunch(uint8_t x, uint8_t y, int32_t aboveLaunchAlt);
void display_altAboveLaunch(uint8_t x, uint8_t y, int32_t aboveLaunchAlt, const uint8_t* font);

void display_alt_type(uint8_t cursor_x, uint8_t cursor_y, const uint8_t* font, uint8_t altType);
void print_alt_label(uint8_t altType);

enum altTypes {
  altType_MSL,
  altType_GPS,
  altType_aboveWaypoint,

  altType_aboveLaunch,
  altType_aboveGoal,
  altType_aboveLZ,
  altType_AGL
};

// The vertical bar graph of climb/sink rate.
// Different Climb & Sink heights allow the bar to be asymmetric
void display_varioBar(uint8_t varioBarTop,
                      uint8_t varioBarClimbHeight,
                      uint8_t varioBarSinkHeight,
                      uint8_t varioBarWidth,
                      int32_t displayClimbRate);

// black box with left-facing arrow pointing at vario bar
void display_climbRatePointerBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t triSize);
// print climb rate in negative color with +/- sign
void display_climbRate(uint8_t x, uint8_t y, const uint8_t* font, int16_t displayClimbRate);
// print climb rate in negative color without +/- sign, and adjust font size to fit in small box
// (for nav page)
void display_unsignedClimbRate_short(uint8_t x, uint8_t y, int16_t displayClimbRate);

void display_accel(uint8_t x, uint8_t y, float accel);
void display_glide(uint8_t x, uint8_t y, float glide);
void display_temp(uint8_t x, uint8_t y, int16_t temperature);
void display_humidity(uint8_t x, uint8_t y, uint8_t temperature);

void display_battIcon(uint8_t x, uint8_t y, bool vertical);
void display_batt_charging_fullscreen(uint8_t x, uint8_t y);
void display_GPS_icon(uint8_t x, uint8_t y);
void display_fanet_icon(const uint8_t& x, const uint8_t& y);

void display_windSockArrow(int16_t x, int16_t y, int16_t radius);
void display_windSockRing(int16_t x, int16_t y, int16_t radius, int16_t size, bool showPointer);
void display_windSpeedCentered(uint8_t x, uint8_t y, const uint8_t* font);
void displayWaypointDropletPointer(uint8_t centerX,
                                   uint8_t centerY,
                                   uint8_t pointRadius,
                                   float direction);

void display_menuTitle(String title);

void display_headerAndFooter(bool timerSelected, bool showTurnArrows);

void display_off_splash(void);
void display_on_splash(void);
void display_splashLogo(void);

#endif