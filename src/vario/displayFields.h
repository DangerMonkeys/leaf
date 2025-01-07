#ifndef displayFields_h
#define displayFields_h

#include <Arduino.h>

void display_selectionBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tri);

void display_clockTime(uint8_t x, uint8_t y, bool show_ampm);
void display_flightTimer(uint8_t x, uint8_t y, bool shortstring, bool selected);
void display_waypointTimeRemaining(uint8_t x, uint8_t y, const uint8_t *font);
void display_speed(uint8_t cursor_x, uint8_t cursor_y);
void display_speed(uint8_t x, uint8_t y, const uint8_t *font);
void display_speed(uint8_t x, uint8_t y, const uint8_t *font, bool units);
void display_distance(uint8_t cursor_x, uint8_t cursor_y, double distance);
void display_heading(uint8_t cursor_x, uint8_t cursor_y, bool degSymbol);
void display_headingTurn(uint8_t cursor_x, uint8_t cursor_y);

void display_alt(uint8_t cursor_x, uint8_t cursor_y, const uint8_t *font, int32_t displayAlt);
void display_altAboveLaunch(uint8_t x, uint8_t y, int32_t aboveLaunchAlt);
void display_altAboveLaunch(uint8_t x, uint8_t y, int32_t aboveLaunchAlt, const uint8_t *font);

void display_alt_type(uint8_t cursor_x, uint8_t cursor_y, const uint8_t *font, uint8_t altType);
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

void display_varioBar(uint8_t varioBarFrame_top,
                      uint8_t varioBarFrame_length,
                      uint8_t varioBarFrame_width,
                      int32_t displayBarClimbRate);
void display_climbRatePointerBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t triSize);
void display_climbRate(uint8_t x, uint8_t y, const uint8_t *font, int16_t displayClimbRate);

void display_accel(uint8_t x, uint8_t y, float accel);
void display_glide(uint8_t x, uint8_t y, float glide);
void display_temp(uint8_t x, uint8_t y, int16_t temperature);
void display_humidity(uint8_t x, uint8_t y, uint8_t temperature);

void display_battIcon(uint8_t x, uint8_t y, bool vertical);
void display_batt_charging_fullscreen(uint8_t x, uint8_t y);
void display_GPS_icon(uint8_t x, uint8_t y);

void display_windSock(int16_t x, int16_t y, int16_t radius, float wind_angle);

void display_headerAndFooter(bool headingShowTurn, bool timerSelected);

void display_off_splash(void);
void display_on_splash(void);
void display_splashLogo(void);

#endif