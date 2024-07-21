/*
 * display.cpp
 *
 *
 */
#include <Arduino.h>
#include <U8g2lib.h>

#include "display.h"
#include "display_tests.h"
#include "fonts.h"
#include "pages.h"

#include "Leaf_SPI.h"
#include "gps.h"
#include "baro.h"
#include "power.h"
#include "log.h"
#include "settings.h"
#include "speaker.h"

//#define GLCD_RS LCD_RS
//#define GLCD_RESET LCD_RESET

// Display Testing Temp Vars
float wind_angle = 1.57;
char seconds = 0;
char minutes = 0;
char hours = 0;
uint16_t heading = 0;
char string_heading[] = " WNW ";
char string_satNum[] = "00";
char string_gpsLat[] = "0000000000";
char string_gpsLng[] = "0000000000";

U8G2_ST7539_192X64_F_4W_HW_SPI u8g2(U8G2_R3, SPI_SS_LCD, LCD_RS, LCD_RESET);

int8_t display_page = page_thermal;
uint8_t display_page_prior = page_thermal; // track the page we used to be on, so we can "go back" if needed (like cancelling out of a menu heirarchy)

void display_init(void) {
  pinMode(SPI_SS_LCD, OUTPUT);
  digitalWrite(SPI_SS_LCD, HIGH);
  u8g2.setBusClock(20000000);
  Serial.print("u8g2 set clock. ");
  u8g2.begin();
  Serial.print("u8g2 began. ");
  u8g2.setContrast(80);
  Serial.print("u8g2 set contrast. ");

  pinMode(LCD_BACKLIGHT, OUTPUT);
  Serial.println("u8g2 done. ");
}


void display_turnPage(uint8_t action) {
  uint8_t tempPage = display_page;
  
  switch (action) {
    case page_home: 
      display_page = page_thermal;
      break;
      
    case page_next:
      display_page++;
      if (display_page == page_last) display_page = 0;
      break;

    case page_prev:
      display_page--;
      if (display_page < 0) display_page = page_last - 1;
      break;

    case page_back:
      speaker_playSound(fx_cancel);      
      display_page = display_page_prior;      
  }
  


  if (display_page != tempPage) display_page_prior = tempPage;
}

void display_setPage(uint8_t targetPage) {
  uint8_t tempPage = display_page;  
  display_page = targetPage;
  
  if (display_page != tempPage) display_page_prior = tempPage;
}


uint8_t display_getPage() {
  return display_page;
}

void display_update() {
  switch (display_page) {
    case page_thermal:
      display_page_thermal();
      break;
    case page_sats:
      gps_test_sats();
      break;
    case page_nav:
      display_page_nav();
      break;
    case page_menu:
      display_page_menu();
      break;
    case page_charging:
      display_page_charging();
      break;
    case page_menu_units:
      units_page.draw();
      break;
  }  
}

void display_clear() {
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


char speed[] = "132";
char windSpeed[] = "28";
char turn = 1;
uint16_t windDir = 235;
int32_t varioBar_climbRate = -100;     // cm/s  (i.e. m/s * 100)
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
  if  (varioBar_climbRate > 1100) {
    climbChange *= -1;
    varioBar_climbRate = 1090;
  } else if (varioBar_climbRate < -1100) {
    climbChange *= -1;
    varioBar_climbRate = -1090;
  }
}



/********************************************************************************************/
// Display Components
// Individual fields that can be called from many different pages, and/or placed in different positions

    void display_flightTimer(uint8_t x, uint8_t y, bool shortstring) {
      uint8_t h = 16;
      uint8_t w = 44;
      if (shortstring) w = 34;

      u8g2.setDrawColor(1);
      u8g2.drawRBox(x, y-h, w, h, 2);
      u8g2.setDrawColor(0);
      u8g2.setFont(leaf_6x12);
      u8g2.setCursor(x+2, y-2);
      u8g2.print(log_getFlightTimerString(shortstring));
      u8g2.setDrawColor(1);
    }



    // Display speed (overloaded function allows for with or without font setting and unit character)
    uint8_t display_speed(uint8_t cursor_x, uint8_t cursor_y) {    
      uint16_t displaySpeed = gps.speed.mph() + 0.5;   // add half so we effectively round when truncating from float to int.
      if (displaySpeed >= 1000) displaySpeed = 999;       // cap display value at 3 digits

      if (displaySpeed < 10) cursor_x += 6; // leave a space if only 1 digit
      u8g2.setCursor(cursor_x, cursor_y);    
      u8g2.print(displaySpeed, 1);
      if (displaySpeed < 100) cursor_x = 13;
      else cursor_x = 19;
      return cursor_x;    // keep track of whether we printed a 3 digit or 2 digit speed value
    }
    uint8_t display_speed(uint8_t x, uint8_t y, const uint8_t *font) {     
      u8g2.setFont(font);
      return display_speed(x, y);
    }    
    uint8_t display_speed(uint8_t x, uint8_t y, const uint8_t *font, bool units) {
      uint8_t cursor_x = display_speed(x, y, font);
      //kpm or mph
      if (1) u8g2.print((char)135);  // unit character
      else   u8g2.print((char)136);  // unit character   
      return (cursor_x += 7);
    }

    void display_headingTurn(uint8_t cursor_x, uint8_t cursor_y) {
      u8g2.setCursor(cursor_x, cursor_y);
      u8g2.setFont(leaf_7x10);

      //Left turn arrow if needed
      char displayTurn = '=' + gps_getTurn();   // in the 7x10 font, '=' is the "no turn" center state; 3 chars to each side are incresing amounts of turn arrow
      if (displayTurn < '=') u8g2.print(displayTurn);  

      // Cardinal heading direction  
      const char *displayHeadingCardinal = gps.cardinal(gps.course.deg());  //gps_getCourseCardinal();  
      if      (strlen(displayHeadingCardinal) == 1) u8g2.setCursor(cursor_x + 16, cursor_y);
      else if (strlen(displayHeadingCardinal) == 2) u8g2.setCursor(cursor_x + 12, cursor_y);
      else                                          u8g2.setCursor(cursor_x +  8, cursor_y);

      u8g2.print(displayHeadingCardinal);

      //Right turn arrow if needed
      u8g2.setCursor(cursor_x + 32, cursor_y);
      if (displayTurn > '=') u8g2.print(displayTurn);  
    }


    void display_alt(uint8_t cursor_x, uint8_t cursor_y, const uint8_t *font, int32_t displayAlt) {      
      if (/*ft unit preference*/ false) displayAlt = displayAlt *100 / 3048; // convert cm to ft
      else displayAlt /= 100;    //convert from cm to m

      u8g2.setCursor(cursor_x, cursor_y);  
      u8g2.setFont(font);
      uint8_t fontWidth = 9; // char width plus space  
      if (font == leaf_8x14) fontWidth = 9;
      else if (font == leaf_6x12) fontWidth = 7;
      else if (font == leaf_5h) fontWidth = 6;  
      
      if (displayAlt < 0) {
        u8g2.print('-');
        displayAlt *= -1;
        if (displayAlt > 9999) displayAlt = 9999;        // max string size if negative
      } else if (displayAlt > 99999) displayAlt = 99999; // max string size if positive

      uint8_t digits = 0;
      bool keepZeros = 0;

      // Thousands piece
      if (displayAlt > 999) {    
        digits = displayAlt/1000;
        displayAlt -= (1000 * digits); // keep the last 3 digits
        keepZeros = 1;                            // keep leading zeros for rest of digits since we printed something in thousands place
        if (digits < 10) u8g2.setCursor(cursor_x + fontWidth, cursor_y);
        u8g2.print(digits);    
        u8g2.print(',');    
      }
      // rest of the number
      u8g2.setCursor(cursor_x += (2*fontWidth+3), cursor_y);    // the +3 is for the comma  
      if (keepZeros) {     
        for (int i = 100; i > 0; i /= 10) {
          digits = displayAlt/i;
          displayAlt -= (digits * i);
          u8g2.print(digits);
          u8g2.setCursor(cursor_x += fontWidth, cursor_y);
        }
      } else { // no leading zeros for altitudes less than 1000
        if (displayAlt < 100) cursor_x += fontWidth;
        if (displayAlt <  10) cursor_x += fontWidth;
        u8g2.setCursor(cursor_x, cursor_y);
        u8g2.print(displayAlt);
      }      
    }

    void display_varioBar(uint8_t varioBarFrame_top, uint8_t varioBarFrame_length, uint8_t varioBarFrame_width, int32_t displayBarClimbRate) {
      int16_t varioBar_climbRateMax = 500;   // this is the bar height, but because we can fill then empty the bar, we can show twice this climb value
      int16_t varioBar_climbRateMin = -500;  // again, bar height, so we can show twice this sink value

      uint8_t varioBarFrame_mid = varioBarFrame_top+varioBarFrame_length/2;
      
      u8g2.drawFrame(0, varioBarFrame_top, varioBarFrame_width, varioBarFrame_length);
      
      //u8g2.setDrawColor(0);       
      //u8g2.drawBox(1, varioBarFrame_top+1, varioBarFrame_length-2, varioBarFrame_width-2);    //only needed if varioBar overlaps something else
      //u8g2.setDrawColor(1); 

      // Fill varioBar
      uint8_t varioBarFill_top = varioBarFrame_top+1;    
      uint8_t varioBarFill_bot = varioBarFrame_top+varioBarFrame_length-2;
      uint8_t varioBarFill_top_length = varioBarFrame_mid - varioBarFill_top+1;
      uint8_t varioBarFill_bot_length = varioBarFill_bot-varioBarFrame_mid+1;

      int16_t varioBarFill_pixels = 0;
      uint8_t varioBarFill_start = 1;
      uint8_t varioBarFill_end = 1;

      if (displayBarClimbRate > 2 * varioBar_climbRateMax || displayBarClimbRate < 2 * varioBar_climbRateMin) {
        // do nothing, the bar is maxxed out which looks empty
        
      } else if (displayBarClimbRate > varioBar_climbRateMax) {      
        // fill top half inverted
        varioBarFill_pixels = varioBarFill_top_length * (displayBarClimbRate - varioBar_climbRateMax) / varioBar_climbRateMax;
        varioBarFill_start = varioBarFill_top;
        varioBarFill_end   = varioBarFrame_mid - varioBarFill_pixels;

      } else if (displayBarClimbRate < varioBar_climbRateMin) {      
        // fill bottom half inverted
        varioBarFill_pixels = varioBarFill_bot_length * (displayBarClimbRate - varioBar_climbRateMin) / varioBar_climbRateMin;
        varioBarFill_start = varioBarFrame_mid + varioBarFill_pixels;
        varioBarFill_end   = varioBarFill_bot;      
        
      } else if (displayBarClimbRate < 0) {      
        // fill bottom half positive
        varioBarFill_pixels = varioBarFill_bot_length * (displayBarClimbRate) / varioBar_climbRateMin;      
        varioBarFill_start = varioBarFrame_mid;
        varioBarFill_end   = varioBarFrame_mid + varioBarFill_pixels;      
        
      } else {      
        // fill top half positive
        varioBarFill_pixels = varioBarFill_top_length * (displayBarClimbRate) / varioBar_climbRateMax;      
        varioBarFill_start = varioBarFrame_mid - varioBarFill_pixels;      
        varioBarFill_end   = varioBarFrame_mid;
      }

      u8g2.drawBox(1, varioBarFill_start, 12, varioBarFill_end - varioBarFill_start + 1);
      
      // Tick marks on varioBar 
      uint8_t tickSpacing = varioBarFill_top_length / 5;  // start with top half tick spacing
      uint8_t line_y = varioBarFrame_top;

      for (int i = 1; i <= 9; i ++) {
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
          u8g2.drawLine(1, line_y, 6, line_y);
        }
      }
    }

    void display_climbRatePointerBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t triSize, int16_t displayClimbRate) {
      bool climbInFPM = false;
      float climbInMS = 0;
      u8g2.setDrawColor(1);  
      u8g2.drawBox(x, y, w, h);
      u8g2.drawTriangle(x-triSize, y+h/2, x-1, y+h/2-triSize, x-1, y+h/2+triSize);
        
      u8g2.setCursor(x, y += 16);   //scoot cursor down for bottom justified font
      u8g2.setFont(leaf_8x14);
      uint8_t fontWidth = 9;        // character size plus space
      u8g2.setDrawColor(0);

      if (displayClimbRate > 0) u8g2.print('+');
      else if (displayClimbRate < 0) {
        u8g2.print('-');
        displayClimbRate *= -1; // keep positive part
      }
      x += fontWidth;

      if (climbInFPM) {
        displayClimbRate = displayClimbRate * 197 / 100;    // convert from cm/s to fpm
        if (displayClimbRate < 1000) x += fontWidth;
        if (displayClimbRate < 100 ) x += fontWidth;
        if (displayClimbRate < 10  ) x += fontWidth;
        u8g2.setCursor(x, y);
        u8g2.print(displayClimbRate);
      } else {
        displayClimbRate = (displayClimbRate + 5) / 10;     // lose one decimal place and round off in the process
        climbInMS = (float)displayClimbRate/10;             // convert to float for ease of printing with the decimal in place
        if (climbInMS < 10  ) x += fontWidth;
        u8g2.setCursor(x, y);
        u8g2.print(climbInMS, 1);
      }
      u8g2.setDrawColor(1);   // always set back to 1 if we've been using 0, just in case the next draw function forgets  
    }

    void display_altAboveLaunch(uint8_t x, uint8_t y, int32_t aboveLaunchAlt) {
      u8g2.setCursor(x, y - 16);
      u8g2.setFont(leaf_5h);
      u8g2.print("Above Launch");
      display_alt(x, y, leaf_6x12, aboveLaunchAlt);
    }

    void display_temp(uint8_t x, uint8_t y, uint16_t temperature) {
      u8g2.setCursor(x, y);
      u8g2.setDrawColor(1);
      u8g2.setFont(leaf_6x12);
      u8g2.print(temperature/100);
      u8g2.print((char)133);
      u8g2.setCursor(x, y+14);
      u8g2.print(temperature * 9 / 500 + 32);
      u8g2.print((char)134);
    }


    void display_battIcon(uint8_t x, uint8_t y) {
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
      uint16_t battMV = power_getBattLevel(1);
      uint16_t battADC = power_getBattLevel(2);
      char battIcon = '0' + (battPercent+5)/10;
      if (power_getBattCharging()) {
        if (battPercent >= 100) battIcon = ';';
        else battIcon = '/';
      }


      u8g2.setCursor(x, y);
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
      */
      u8g2.setFont(leaf_6x12);
      u8g2.setCursor(x, y+=15);
      u8g2.print(battPercent);
      u8g2.setCursor(x, y+=15);
      u8g2.print(battMV);
      u8g2.setCursor(x, y+=15);
      u8g2.print(battADC);
    }

    uint8_t batt_percent_testing = 0;
    uint8_t batt_charging_testing = 1;
    int8_t batt_testing_direction = 1;

    void display_batt_charging_fullscreen() {

      // position of battery
        uint8_t x = 32; // center of battery left/right
        uint8_t y = 20; // top of battery nib

      // size of battery
        uint8_t w = 60;//60;
        uint8_t h = 120;//140;
        uint8_t t = 2;    // wall thickness
      
      // Battery Canister
        u8g2.setDrawColor(1);
        u8g2.drawRBox(x-w/5, y-1, w/5*2, w/3, w/22);               // battery tip nub
        u8g2.drawRBox(x-(w/2), y+w/20, w, h, w/15);               // main rectangle outline
        u8g2.setDrawColor(0);
        u8g2.drawRBox(x-(w/2)+t, y+w/20+t, w-2*t, h-2*t, w/15-t);   // empty internal volume

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

        uint8_t fill_h = (h-4*t)*battPercent/100;
        uint8_t fill_y = (y+w/20+2*t)+((h-4*t) - fill_h); // (starting position to allow for line thickness etc) + ( (100% height) - (actual height) )
        u8g2.setDrawColor(1);
        u8g2.drawBox(x-(w/2)+2*t, fill_y, w-4*t, fill_h);//, w/8-t/2);

      // Charging bolt
      if (power_getBattCharging()) { //batt_charging_testing) {
        uint8_t bolt_x1 = w*6/100;      //  4
        uint8_t bolt_x2 = w*7/100;      //  6
        uint8_t bolt_x3 = w*21/100;     // 17
        uint8_t bolt_y1 = h*3/100;      //  2
        uint8_t bolt_y2 = h*25/100;     // 19
        uint8_t bolt_y = y+h/2;     // center of bolt in y direction

        u8g2.setDrawColor(0);
        u8g2.drawTriangle(x+bolt_x1, bolt_y+bolt_y1, x+bolt_x2, bolt_y-bolt_y2, x-bolt_x3, bolt_y+bolt_y1);
        u8g2.drawTriangle(x-bolt_x1, bolt_y-bolt_y1, x-bolt_x2, bolt_y+bolt_y2, x+bolt_x3, bolt_y-bolt_y1);

        for (int i = 0; i < 4; i++) {
          if (i == 0) {           x--; }  // drag the bolt outline around to make it thicker
          if (i == 1) { bolt_y++;      }  // 
          if (i == 2) { bolt_y--; x++; }  // 
          if (i == 3) { bolt_y++;      }  //

          u8g2.setDrawColor(1);
          u8g2.drawLine(x-bolt_x3, bolt_y+bolt_y1, x+bolt_x2, bolt_y-bolt_y2);
          u8g2.drawLine(x+bolt_x1, bolt_y-bolt_y1, x+bolt_x2, bolt_y-bolt_y2);
          u8g2.drawLine(x+bolt_x1, bolt_y-bolt_y1, x+bolt_x3, bolt_y-bolt_y1);
          u8g2.drawLine(x-bolt_x2, bolt_y+bolt_y2, x+bolt_x3, bolt_y-bolt_y1);
          u8g2.drawLine(x-bolt_x2, bolt_y+bolt_y2, x-bolt_x1, bolt_y+bolt_y1);
          u8g2.drawLine(x-bolt_x3, bolt_y+bolt_y1, x-bolt_x1, bolt_y+bolt_y1);
        }
      }

      uint16_t battMV = power_getBattLevel(1);
      uint16_t battADC = power_getBattLevel(2);

      y += h+3;
      x = 18;

      u8g2.setFont(leaf_6x12);
      u8g2.setCursor(x, y+=15);
      u8g2.print(battPercent);
      u8g2.print('%');

      u8g2.setCursor(x, y+=15);
      u8g2.print(battMV);
      u8g2.setCursor(x, y+=15);
      u8g2.print(battADC);
    }
// END OF COMPONENT DISPLAY FUNCTIONS //
/********************************************************************************************/



/*********************************************************************************
**   CHARGING PAGE    ************************************************************
*********************************************************************************/
void display_page_charging() {
  
  u8g2.firstPage();
  do { 

    display_batt_charging_fullscreen();


    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(18, 14);  
    if      (power_getInputCurrent() == 0)  u8g2.print("100mA");
    else if (power_getInputCurrent() == 1)  u8g2.print("500mA");
    else if (power_getInputCurrent() == 2)  u8g2.print("8100mA");
    else if (power_getInputCurrent() == 3)  u8g2.print("OFF");
    
    

  } while ( u8g2.nextPage() ); 
  
}

/*********************************************************************************
**    NAV PAGE        ************************************************************
*********************************************************************************/
void display_page_nav() {
  baro_updateFakeNumbers();
  gps_updateFakeNumbers();

  u8g2.firstPage();
  do { 
    uint8_t x = display_speed(0,12);
    display_headingTurn(x+3, 10);
    display_alt(17, 26, leaf_8x14, baro_getAlt());

  } while ( u8g2.nextPage() ); 
  
}

/*********************************************************************************
**    THERMAL PAGE        ********************************************************
*********************************************************************************/
void display_page_thermal() {
  //baro_updateFakeNumbers();
  //gps_updateFakeNumbers();
  //display_update_temp_vars();

  u8g2.firstPage();
  do { 
    //speed
    u8g2.setFont(leaf_6x12);
    uint8_t x = display_speed(0,12);    // grab resulting x cursor value (if speed has 2 or 3 digits, things will shift over)

    //heading
    display_headingTurn(x+3, 10);

    //air data
    display_alt(17, 26, leaf_8x14, baro_getAlt());
    display_altAboveLaunch(17, 50, baro_getAltAboveLaunch());
    display_varioBar(13, 111, 14, baro_getClimbRate());
    display_climbRatePointerBox(14, 59, 50, 17, 6, baro_getClimbRate());     // x, y, w, h, triangle size
    
    display_temp(40, 174, baro_getTemp());

    // batt stuff
    display_battIcon(20, 90);

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(40, 90);    
    u8g2.print(power_getInputCurrent());

    //flight timer
    display_flightTimer(2, 156, 0);
    display_flightTimer(2, 176, 1);
    u8g2.setCursor(2, 192);
    u8g2.print(log_getFlightTimerSec());

    //u8g2.drawBox(0,154,64,38);

  } while ( u8g2.nextPage() ); 
  
}


/*********************************************************************************
**    MAIN MENU PAGE      ********************************************************
*********************************************************************************/
void display_page_menu() {

  u8g2.firstPage();
  do { 
    // Title(s) 
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 14);
    u8g2.setDrawColor(1);
    u8g2.print("Main Menu");
    u8g2.drawHLine(0, 16, 64);

    u8g2.setCursor(0, 30);
    u8g2.print("Volume:");
    u8g2.setCursor(0, 75);
    u8g2.print("Units:");


  // Menu Items
    uint8_t start_y = 30;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 44;
    uint8_t i = 0;
    uint8_t menu_items_y[] = {45, 60, 90, 105, 120, 135};
     
     
    u8g2.setCursor(setting_name_x, menu_items_y[i]);
    u8g2.print("Vario");
    u8g2.setCursor(setting_choice_x+11, menu_items_y[i]);    
    u8g2.print(VOLUME_VARIO);

    i++;
    u8g2.setCursor(setting_name_x, menu_items_y[i]);
    u8g2.print("System");
    u8g2.setCursor(setting_choice_x+11, menu_items_y[i]);    
    u8g2.print(VOLUME_SYSTEM);
    
    i++;
    u8g2.setCursor(setting_name_x, menu_items_y[i]);
    u8g2.print("Alt");
    u8g2.setCursor(setting_choice_x, menu_items_y[i]); 
    if (UNITS_alt) u8g2.print("ft");
    else u8g2.print(" m");

    i++;
    u8g2.setCursor(setting_name_x, menu_items_y[i]);
    u8g2.print("Speed");
    u8g2.setCursor(setting_choice_x, menu_items_y[i]); 
    if (UNITS_speed) u8g2.print("mph");
    else u8g2.print("kph");

    i++;
    u8g2.setCursor(setting_name_x, menu_items_y[i]);
    u8g2.print("Climb");
    u8g2.setCursor(setting_choice_x, menu_items_y[i]); 
    if (UNITS_speed) u8g2.print("fpm");
    else u8g2.print("m/s");



  } while ( u8g2.nextPage() ); 


}


/*********************************************************************************
**    SATELLITES PAGE     ********************************************************
*********************************************************************************/

// draw satellite constellation starting in upper left x, y and box size (width = height)
void display_satellites(uint16_t x, uint16_t y, uint16_t size) {
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
        u8g2.setCursor(0,40);
        u8g2.print(gps.course.deg(), 0);
        u8g2.setCursor(30,40);
        u8g2.print(gps.cardinal(gps.course.deg()));
        u8g2.setFont(leaf_5h);
        u8g2.drawStr(0, 27, "Heading");

      // altitude
        u8g2.setFont(leaf_6x12);
        u8g2.setCursor(0,60);
        u8g2.print(gps.altitude.meters());
        u8g2.setFont(leaf_5h);
        u8g2.drawStr(0, 47, "alt");


    // Draw the satellite background
    u8g2.setDrawColor(0);
    u8g2.drawBox(x, y, size, size);   // clear the box drawing area
    u8g2.setDrawColor(1);
    u8g2.drawCircle(x+size/2, y+size/2, size/2);   // the horizon circle
    u8g2.drawCircle(x+size/2, y+size/2, size/4);   // the 45deg elevation circle

    // Draw the satellites
    for (int i = MAX_SATELLITES -1; i>=0; i--) { 
      if (sats[i].active) {
        /*
        Serial.print("act: ");
        Serial.print(sats[i].active);
        Serial.print(" el: ");
        Serial.print(sats[i].elevation);
        Serial.print(" az: ");
        Serial.print(sats[i].azimuth);
        Serial.print(" snr: ");
        Serial.println(sats[i].snr);
        */

        // Sat location (on circle display)
        uint16_t radius = (90 - sats[i].elevation) * size/2 / 90;
        int16_t sat_x = sin(sats[i].azimuth*PI/180)*radius;
        int16_t sat_y = - cos(sats[i].azimuth*PI/180)*radius;

        // Draw disc
        /*
        u8g2.drawDisc(size/2+sat_x, size/2+sat_y, size/16);
        u8g2.setDrawColor(0);
        u8g2.drawCircle(size/2+sat_x, size/2+sat_y, size/16+1);
        u8g2.setDrawColor(1);
        */

        // Draw box with numbers
        uint16_t x_pos = x + size/2 + sat_x;
        uint16_t y_pos = y + size/2 + sat_y;

        u8g2.setFont(u8g2_font_micro_tr);          // Font for satellite numbers
        if (sats[i].snr < 20) {                    
          u8g2.drawFrame(x_pos-5, y_pos-4, 11, 9); // white box with black border if SNR is low
          u8g2.setDrawColor(0);
          u8g2.drawBox(x_pos-4, y_pos-3, 9, 7); // erase the gap between frame and text
          u8g2.setDrawColor(1);
        } else {
          u8g2.drawBox(x_pos-4, y_pos-3, 9, 7);    // black box if SNR is high
          u8g2.setDrawColor(0);                    // .. with white font inside box
        }

        if (i<9) {
          u8g2.drawStr(x_pos-3, y_pos+3, "0");
          x_pos += 4;
        }
        u8g2.drawStr(x_pos-3, y_pos+3, itoa(i+1,string_satNum, 10));
        u8g2.setDrawColor(1);                
      }
     
    }
    
   
    //draw other GPS stuff just for testing purposes
    u8g2.drawStr(0, size + y + 10, "Lat:");
    u8g2.setCursor(16, size + y + 10);
    u8g2.print(gps.location.lat(), 10);

    u8g2.drawStr(0, size + y + 20, "Lon:");
    u8g2.setCursor(16, size + y + 20);
    u8g2.print(gps.location.lng(), 10);

  } while ( u8g2.nextPage() );
}






/****************************/
//      TESTING STUFF       /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/****************************/


void display_test(void) {  
  delay(100);
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_tinyunicode_tf);
    u8g2.drawStr(49, 18, "mph");

    u8g2.setFont(leaf_6x12);
    u8g2.drawStr(44, 12, "103");       // speed

    
    delay(30);
    //heading++;
    heading = wind_angle*360/(2 * PI)+180;
    heading = heading % 360;
    
    
    if (heading == 360) heading = 0;
    if (heading < 12 || heading >= 349) strcpy(string_heading, "  N >"); 
    else if (heading < 34 ) strcpy(string_heading, " NNE>");
    else if (heading < 57 ) strcpy(string_heading, "  NE>");
    else if (heading < 79 ) strcpy(string_heading, " ENE>");
    else if (heading < 102) strcpy(string_heading, "  E >");
    else if (heading < 124) strcpy(string_heading, " ESE>");
    else if (heading < 147) strcpy(string_heading, "  SE>");
    else if (heading < 169) strcpy(string_heading, " SSE>");
    else if (heading < 192) strcpy(string_heading, "< S  "); 
    else if (heading < 214) strcpy(string_heading, "<SSW ");
    else if (heading < 237) strcpy(string_heading, "< SW ");
    else if (heading < 259) strcpy(string_heading, "<WSW ");
    else if (heading < 282) strcpy(string_heading, "< W  ");
    else if (heading < 304) strcpy(string_heading, "<WNW ");
    else if (heading < 327) strcpy(string_heading, "< NW ");
    else if (heading < 349) strcpy(string_heading, "<NNW ");
    else strcpy(string_heading, "<999>");



    u8g2.drawStr( 0, 12, string_heading);  // direction


    uint16_t nav_circle_center_x = 26;
    uint16_t nav_circle_center_y = 41;
    uint16_t nav_circle_radius   = nav_circle_center_x;   // set to x dimension for left-justified nav circle

    u8g2.drawCircle(nav_circle_center_x,nav_circle_center_y,nav_circle_radius);
    u8g2.drawCircle(nav_circle_center_x,nav_circle_center_y,nav_circle_radius-1);
    u8g2.drawCircle(nav_circle_center_x,nav_circle_center_y,nav_circle_radius*2/3);
    u8g2.drawCircle(nav_circle_center_x,nav_circle_center_y,nav_circle_radius*1/3);
  
    // Wind Triangle
    uint16_t wind_triangle_radius = 10;
    wind_angle += .01;
    if (wind_angle > 2 * PI) wind_angle = 0;

    uint16_t wind_triangle_center_x = nav_circle_center_x;
    uint16_t wind_triangle_center_y = nav_circle_center_y+nav_circle_radius/2;    
    int16_t wind_triangle_tip_len = wind_triangle_radius-1;  // pixels from center for point
    int16_t wind_triangle_tail_len = wind_triangle_radius-1;  // pixels from center for the tails
    float wind_triangle_tail_angle = 0.65;  // pixels tail width (half-width)

    u8g2.setDrawColor(0);
    u8g2.drawDisc(wind_triangle_center_x, wind_triangle_center_y, wind_triangle_radius);
    u8g2.setDrawColor(1);
    u8g2.drawCircle(wind_triangle_center_x, wind_triangle_center_y, wind_triangle_radius);        
    u8g2.drawTriangle(32, 96, 36, 106, 28, 106);

    uint16_t tip_xprime = wind_triangle_center_x + sin(wind_angle + PI)*wind_triangle_tip_len;
    uint16_t tip_yprime = wind_triangle_center_y - cos(wind_angle + PI)*wind_triangle_tip_len;
    uint16_t tail_1_xprime = wind_triangle_center_x + sin(wind_angle + wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_1_yprime = wind_triangle_center_y - cos(wind_angle + wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_2_xprime = wind_triangle_center_x + sin(wind_angle - wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_2_yprime = wind_triangle_center_y - cos(wind_angle - wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_mid_xprime = wind_triangle_center_x + sin(wind_angle)*wind_triangle_tail_len/2;
    uint16_t tail_mid_yprime = wind_triangle_center_y - cos(wind_angle)*wind_triangle_tail_len/2;
    
    u8g2.drawTriangle(tip_xprime, tip_yprime, tail_1_xprime, tail_1_yprime, tail_mid_xprime, tail_mid_yprime);
    u8g2.drawTriangle(tip_xprime, tip_yprime, tail_2_xprime, tail_2_yprime, tail_mid_xprime, tail_mid_yprime);
    u8g2.drawLine(tip_xprime, tip_yprime, tail_1_xprime, tail_1_yprime);
    u8g2.drawLine(tail_mid_xprime, tail_mid_yprime, tail_2_xprime, tail_2_yprime);


/*

    u8g2.drawTriangle(uint16_t(triangle_center_x + sin(wind_angle + PI)*wind_triangle_tip_len),                           // x' tip
                      uint16_t(triangle_center_y - cos(wind_angle + PI)*wind_triangle_tip_len),                           // y' tip
                      uint16_t(triangle_center_x + sin(wind_angle + wind_triangle_tail_angle)*wind_triangle_tail_len),    // x' tail 1
                      uint16_t(triangle_center_y - cos(wind_angle + wind_triangle_tail_angle)*wind_triangle_tail_len),    // y' tail 1
                      uint16_t(triangle_center_x + sin(wind_angle)*wind_triangle_tail_len/2),                             // x' middle tail
                      uint16_t(triangle_center_y - cos(wind_angle)*wind_triangle_tail_len/2)                              // y' middle tail
                      );
    u8g2.drawTriangle(uint16_t(triangle_center_x + sin(wind_angle + PI)*wind_triangle_tip_len),                           // x' tip
                      uint16_t(triangle_center_y - cos(wind_angle + PI)*wind_triangle_tip_len),                           // y' tip
                      uint16_t(triangle_center_x + sin(wind_angle)*wind_triangle_tail_len/2),                             // x' middle tail
                      uint16_t(triangle_center_y - cos(wind_angle)*wind_triangle_tail_len/2),                             // y' middle tail
                      uint16_t(triangle_center_x + sin(wind_angle - wind_triangle_tail_angle)*wind_triangle_tail_len),    // x' tail 2
                      uint16_t(triangle_center_y - cos(wind_angle - wind_triangle_tail_angle)*wind_triangle_tail_len)     // y' tail 2
                      );
    u8g2.drawLine(    uint16_t(triangle_center_x + sin(wind_angle + PI)*wind_triangle_tip_len),                           // x' tip
                      uint16_t(triangle_center_y - cos(wind_angle + PI)*wind_triangle_tip_len),                           // y' tip
                      uint16_t(triangle_center_x + sin(wind_angle + wind_triangle_tail_angle)*wind_triangle_tail_len),    // x' tail 1
                      uint16_t(triangle_center_y - cos(wind_angle + wind_triangle_tail_angle)*wind_triangle_tail_len)    // y' tail 1                     
                      );
*/

/*
    u8g2.drawTriangle(  uint16_t(cos(angle)*wind_triangle_tip_len) + triangle_center_x,       // x' tip
                        uint16_t(sin(angle)*wind_triangle_tip_len) + triangle_center_y,      // y' tip
                        uint16_t(cos(angle)*wind_triangle_width + sin(angle)*wind_triangle_tail_len) + triangle_center_x,  // x' right tail
                        uint16_t(-cos(angle)*wind_triangle_tail_len + sin(angle)*wind_triangle_width) + triangle_center_y, // y' right tail
                        uint16_t(-cos(angle)*wind_triangle_width + sin(angle)*wind_triangle_tail_len) + triangle_center_x, // x' left tail
                        uint16_t(-cos(angle)*wind_triangle_tail_len - sin(angle)*wind_triangle_width) + triangle_center_y // y' right tail
                        );
  */  


                        


    // Waypoint Name
    u8g2.setFont(u8g2_font_12x6LED_tf);
    u8g2.drawStr(0, 80, "MarshallLZ");




    // Timer w/ Inverted Colors
    u8g2.drawRBox(0,146,51,16,3);
    u8g2.setDrawColor(0);
    u8g2.setFontMode(1);
    u8g2.setFont(leaf_6x12);
    u8g2.drawStr(2, 160, "12:34:00");
    u8g2.setFontMode(0);
    u8g2.setDrawColor(1);


    u8g2.setFont(u8g2_font_12x6LED_mn);
    u8g2.drawStr(0, 180, "12:34:00");
    
    //u8g2.drawStr(0,83,"12,345\"");      // altitude


    int v = 17;
    int width = 16;
    /*
    u8g2.drawFrame(64-width, 96, width, 48);
    u8g2.drawFrame(64-width, 144, width, 48);
    u8g2.drawBox(64-width, 144-v, width, v);
    */
  } while ( u8g2.nextPage() );  
}



void display_test_big(uint8_t test_page) {
  
//Serial.println("starting LCD stuff");
/* NOT SURE WHAT THIS IS
GLCD_inst(0b10100101);
delay(500);
GLCD_data(0b10100101);
delay(500);
GLCD_inst(0b10100100);
delay(500);
GLCD_data(0b10100100);
delay(500);
*/

/*


  //GO HOME GLCD
  GLCD_inst(0b00000000);  //Column address LSB ->0
  delay(20);
  GLCD_inst(0b00010000);  //Column address MSB ->0
  delay(20);
  GLCD_inst(0b10110000);  //Page address ->0
  delay(20);
*/


  if (test_page == 1) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_main_1[d+page*192]);  
      }
    }    
  } else if (test_page == 2) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_nav_1a[d+page*192]);  
      }
    }    
  } else if (test_page == 3) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_nav_1b[d+page*192]);  
      }
    }   
  } else if (test_page == 4) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_nav_2[d+page*192]);  
      }
    }   
  } else if (test_page == 5) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(offroad_1[d+page*192]);  
      }
    }   
  }


  //digitalWrite(LCD_BACKLIGHT, !digitalRead(LCD_BACKLIGHT));
  //delay(500);
//BIG LCD TEST 
}




void display_test_real_3() {


  u8g2.firstPage();
  do {        

    u8g2.setFont(leaf_6x12);
    u8g2.drawStr(0, 12, speed);
    u8g2.setFont(leaf_5h);
    u8g2.drawStr(15, 18, "MPH");

    // heading and turn
    u8g2.setFont(leaf_7x10);
    //if (turn == 0) string_heading[0] = '<';
    //if (turn == 1) string_heading[4] = '>';    
    string_heading[0] = '<';
    string_heading[4] = '>';    
    u8g2.drawStr(23, 10, string_heading);

    // Nav Circles
    uint8_t nav_x = 38;
    uint8_t nav_y = 38;
    uint8_t nav_r = 25;
    uint8_t wind_r = 8;

    u8g2.setDrawColor(1);
    u8g2.drawDisc(nav_x, nav_y, nav_r);  // Main Circle
    u8g2.setDrawColor(0);
    u8g2.drawDisc(nav_x, nav_y, nav_r-2);  // center empty
    u8g2.setDrawColor(1);
    u8g2.drawDisc(nav_x, nav_y, wind_r);   // Wind Circle
    

    // Straight Arrow Pointer (Travel Direction)
    uint8_t pointer_w = 5;              // half width of arrowhead
    uint8_t pointer_h = 9;             // full height of arrowhead
    uint8_t pointer_x = nav_x;
    uint8_t pointer_y = nav_y-nav_r-2;    //tip of arrow

    u8g2.setDrawColor(0);
    u8g2.drawBox(nav_x-(pointer_w)/2, nav_y-nav_r, pointer_w, 2);
    u8g2.setDrawColor(1);
    // arrow point    
    u8g2.drawLine(pointer_x-pointer_w, pointer_y+pointer_h, pointer_x, pointer_y);
    u8g2.drawLine(pointer_x+pointer_w, pointer_y+pointer_h, pointer_x, pointer_y);
    u8g2.drawLine(pointer_x-pointer_w-1, pointer_y+pointer_h, pointer_x-1, pointer_y);
    u8g2.drawLine(pointer_x+pointer_w+1, pointer_y+pointer_h, pointer_x+1, pointer_y);
    //arrow flats
    u8g2.drawLine(pointer_x-pointer_w, pointer_y+pointer_h, pointer_x-pointer_w/2, pointer_y+pointer_h);
    u8g2.drawLine(pointer_x+pointer_w, pointer_y+pointer_h, pointer_x+pointer_w/2, pointer_y+pointer_h);
    //arrow shaft
    u8g2.drawLine(pointer_x-pointer_w/2, pointer_y+pointer_h, pointer_x-pointer_w/2, pointer_y+pointer_h*2);
    u8g2.drawLine(pointer_x+pointer_w/2, pointer_y+pointer_h, pointer_x+pointer_w/2, pointer_y+pointer_h*2);

    // Waypoint Pointer
    uint8_t waypoint_tip_r = 23;
    uint8_t waypoint_shaft_r = 20;    
    uint8_t waypoint_tail_r = 18;
    float waypoint_arrow_angle = 0.205;
    
    int8_t waypoint_tip_x = sin(dirToWypt)*waypoint_tip_r+nav_x;
    int8_t waypoint_tip_y = nav_y-cos(dirToWypt)*waypoint_tip_r;    
    int8_t waypoint_shaft_x = sin(dirToWypt)*waypoint_shaft_r+nav_x;
    int8_t waypoint_shaft_y = nav_y-cos(dirToWypt)*waypoint_shaft_r;    


    u8g2.drawLine(nav_x+1, nav_y, waypoint_shaft_x+1, waypoint_shaft_y);
    u8g2.drawLine(nav_x, nav_y+1, waypoint_shaft_x, waypoint_shaft_y+1);
    u8g2.drawLine(nav_x, nav_y, waypoint_shaft_x, waypoint_shaft_y);          // the real center line; others are just to fatten it up
    u8g2.drawLine(nav_x-1, nav_y, waypoint_shaft_x-1, waypoint_shaft_y);
    u8g2.drawLine(nav_x, nav_y-1, waypoint_shaft_x, waypoint_shaft_y-1);

    int8_t tail_left_x = sin(dirToWypt - waypoint_arrow_angle) * (waypoint_tail_r) + nav_x;
    int8_t tail_left_y = nav_y - cos(dirToWypt - waypoint_arrow_angle) * (waypoint_tail_r);
    int8_t tail_right_x = sin(dirToWypt + waypoint_arrow_angle) * (waypoint_tail_r) + nav_x;
    int8_t tail_right_y = nav_y - cos(dirToWypt + waypoint_arrow_angle) * (waypoint_tail_r);

    u8g2.drawLine(tail_left_x, tail_left_y, waypoint_tip_x, waypoint_tip_y);
    u8g2.drawLine(tail_right_x, tail_right_y, waypoint_tip_x, waypoint_tip_y);
    u8g2.drawLine(tail_right_x, tail_right_y, tail_left_x, tail_left_y);
    u8g2.drawTriangle(tail_left_x, tail_left_y, waypoint_tip_x, waypoint_tip_y, tail_right_x, tail_right_y);




    // Wind Vector
    u8g2.setDrawColor(0);
    display_drawTrianglePointer(nav_x, nav_y, wind_angle, 7);
    u8g2.setDrawColor(1);
    u8g2.setFont(leaf_5x8);
    u8g2.drawStr(53, 19, windSpeed);





    // vario bar ******************************************************          
    int16_t varioBar_climbRateMax = 500;   // this is the bar height, but because we can fill then empty the bar, we can show twice this climb value
    int16_t varioBar_climbRateMin = -500;  // again, bar height, so we can show twice this sink value
    
    uint8_t varioBarFrame_top = 13;
    uint8_t varioBarFrame_length = 111;
    uint8_t varioBarFrame_width = 14;
    uint8_t varioBarFrame_mid = varioBarFrame_top+varioBarFrame_length/2;
    
    u8g2.drawFrame(0, varioBarFrame_top, varioBarFrame_width, varioBarFrame_length);
    //u8g2.setDrawColor(0);       //only needed if nav cricle overlaps vario bar
    //u8g2.drawLine(12, 15, 12, 58);
    //u8g2.setDrawColor(1); 


    // Fill varioBar
    uint8_t varioBarFill_top = varioBarFrame_top+1;    
    uint8_t varioBarFill_bot = varioBarFrame_top+varioBarFrame_length-2;
    uint8_t varioBarFill_top_length = varioBarFrame_mid - varioBarFill_top+1;
    uint8_t varioBarFill_bot_length = varioBarFill_bot-varioBarFrame_mid+1;


    int16_t varioBarFill_pixels = 0;
    uint8_t varioBarFill_start = 1;
    uint8_t varioBarFill_end = 1;

    if (varioBar_climbRate > 2 * varioBar_climbRateMax || varioBar_climbRate < 2 * varioBar_climbRateMin) {
      // do nothing, the bar is maxxed out which looks empty
      
    } else if (varioBar_climbRate > varioBar_climbRateMax) {      
      // fill top half inverted
      varioBarFill_pixels = varioBarFill_top_length * (varioBar_climbRate - varioBar_climbRateMax) / varioBar_climbRateMax;
      varioBarFill_start = varioBarFill_top;
      varioBarFill_end   = varioBarFrame_mid - varioBarFill_pixels;

    } else if (varioBar_climbRate < varioBar_climbRateMin) {      
      // fill bottom half inverted
      varioBarFill_pixels = varioBarFill_bot_length * (varioBar_climbRate - varioBar_climbRateMin) / varioBar_climbRateMin;
      varioBarFill_start = varioBarFrame_mid + varioBarFill_pixels;
      varioBarFill_end   = varioBarFill_bot;      
      
    } else if (varioBar_climbRate < 0) {      
      // fill bottom half positive
      varioBarFill_pixels = varioBarFill_bot_length * (varioBar_climbRate) / varioBar_climbRateMin;      
      varioBarFill_start = varioBarFrame_mid;
      varioBarFill_end   = varioBarFrame_mid + varioBarFill_pixels;      
      
    } else {      
      // fill top half positive
      varioBarFill_pixels = varioBarFill_top_length * (varioBar_climbRate) / varioBar_climbRateMax;      
      varioBarFill_start = varioBarFrame_mid - varioBarFill_pixels;      
      varioBarFill_end   = varioBarFrame_mid;
    }

    u8g2.drawBox(1, varioBarFill_start, 12, varioBarFill_end - varioBarFill_start + 1);

    if  (varioBar_climbRate > 1100) {
      climbChange = -2;
      varioBar_climbRate = 1090;
    } else if (varioBar_climbRate < -1100) {
      climbChange = 2;
      varioBar_climbRate = -1090;
    }



    // Tick marks on varioBar 
    uint8_t tickSpacing = varioBarFill_top_length / 5;  // start with top half tick spacing
    uint8_t line_y = varioBarFrame_top;

    for (int i = 1; i <= 9; i ++) {
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
        u8g2.drawLine(1, line_y, 12, line_y);
      }
    }
    u8g2.setDrawColor(1);    



    // Data Fields *****************************

    uint8_t cursor_y = 57;

    // Climb    
    u8g2.setDrawColor(1);  
    u8g2.drawBox(14, cursor_y, 50, 18);
    u8g2.drawTriangle(8, cursor_y+11, 13, cursor_y+6, 13, cursor_y+16);
    u8g2.setDrawColor(0);
    u8g2.setFont(leaf_8x14);
    u8g2.drawStr(13, cursor_y+16, climbRate);
    u8g2.setDrawColor(1);
    cursor_y += 18;

    // Altitude(s)
    u8g2.setFont(leaf_8x14);
    u8g2.drawStr(16, cursor_y+16, altitude);
    cursor_y += 16;

    // Line divider
    u8g2.drawLine(14, cursor_y+2, 64, cursor_y+2);
    cursor_y += 2;

    // Clock
    u8g2.setFont(leaf_6x12);
    u8g2.drawStr(18, cursor_y+14, clockTime);
    cursor_y +=15;

    // Timer 
    uint8_t timer_x = 14;
    uint8_t timer_y = cursor_y;
    uint8_t timer_w = 50;
    uint8_t timer_h = 16;
    u8g2.drawBox(timer_x, timer_y, timer_w, timer_h);
    u8g2.setDrawColor(0);    
    //u8g2.drawPixel(timer_x, timer_y);
    //u8g2.drawPixel(timer_x, timer_y+timer_h-1);
    //u8g2.drawPixel(timer_x+timer_w-1, timer_y);
    //u8g2.drawPixel(timer_x+timer_w-1, timer_y+timer_h-1);    
    u8g2.setFont(leaf_6x12);
    u8g2.drawStr(timer_x+5, timer_y+timer_h-2, timer);
    u8g2.setDrawColor(1);
    cursor_y += 16;



    //***** FIELD BOXES *****// *****************************************************

    uint8_t temp_cursor = cursor_y;     //save cursor position for next column

    // Time to Waypoint
    u8g2.setFont(leaf_5h);    
    cursor_y += 6;
    u8g2.drawStr(1, cursor_y, "TIME>%");    
    u8g2.setFont(leaf_6x12);
    cursor_y +=13;
    u8g2.drawStr(1, cursor_y, timeToWypt);

    cursor_y = temp_cursor;

    // Dist to Waypoint
    u8g2.setFont(leaf_5h);
    cursor_y += 6;
    u8g2.drawStr(36, cursor_y, "KM>&");
    //u8g2.drawStr(35, 138, "KM>%");
    u8g2.setFont(leaf_6x12);
    cursor_y += 13;
    u8g2.drawStr(38, cursor_y, distToWypt);

    // column divider
    u8g2.drawLine(33, temp_cursor, 33, cursor_y);

    // divider line
    cursor_y += 1;
    u8g2.drawLine(0, cursor_y, 63, cursor_y);
    cursor_y += 1;

    temp_cursor = cursor_y;             //save cursor position for next column

    // Glide over ground now
    u8g2.setFont(leaf_5h);
    cursor_y += 6;
    u8g2.drawStr(1, cursor_y, "`GLIDE");
    u8g2.setFont(leaf_6x12);
    cursor_y += 13;
    u8g2.drawStr(4, cursor_y, glide);

    cursor_y = temp_cursor;

    // Glide to waypoint
    u8g2.setFont(leaf_5h);
    cursor_y += 6;
    u8g2.drawStr(36, cursor_y, "`WAYPT");
    u8g2.setFont(leaf_6x12);
    cursor_y += 13;
    u8g2.drawStr(38, cursor_y, glideToWypt);

    // column divider
    u8g2.drawLine(33, temp_cursor, 33, cursor_y);

    // divider line
    cursor_y += 1;
    //u8g2.drawLine(1, cursor_y, 64, cursor_y);
    //cursor_y += 1;

    // progress bar
    u8g2.drawFrame(0, cursor_y, 64, 4);
    u8g2.drawBox(1, cursor_y+1, 27, 2);  // 3rd argument is filled width from left (% of 64 pixels)
    cursor_y += 4;

    // waypoint name
    u8g2.setFont(u8g2_font_7x14B_tr);
    cursor_y += 11;
    u8g2.drawStr(0, cursor_y, waypoint);
    cursor_y +=1;
    


    //icons
    u8g2.setFont(leaf_icons);
    u8g2.drawStr(0,192,"6");      // Battery
    u8g2.drawStr(14,192, "<");    // SD card
    u8g2.drawStr(24,192, "%");    // bluetooth
    u8g2.drawStr(31,192, "'");    // wifi
    u8g2.drawStr(42,192, "+");    // gps
    u8g2.drawStr(53,192, "#");    // Menu
    



  } while ( u8g2.nextPage() ); 

}


void display_drawTrianglePointer(uint16_t x, uint16_t y, float angle, uint16_t radius) {    
    if (angle > 2 * PI) angle = 0;

    int16_t wind_triangle_tip_len = radius-1;  // pixels from center for point
    int16_t wind_triangle_tail_len = radius-1;  // pixels from center for the tails
    float wind_triangle_tail_angle = 0.65;  // pixels tail width (half-width)
    /*
    u8g2.setDrawColor(0);
    u8g2.drawDisc(wind_triangle_center_x, wind_triangle_center_y, wind_triangle_radius);
    u8g2.setDrawColor(1);
    u8g2.drawCircle(wind_triangle_center_x, wind_triangle_center_y, wind_triangle_radius);        
    */
    uint16_t tip_xprime = x + sin(angle + PI)*wind_triangle_tip_len;
    uint16_t tip_yprime = y - cos(angle + PI)*wind_triangle_tip_len;
    uint16_t tail_1_xprime = x + sin(angle + wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_1_yprime = y - cos(angle + wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_2_xprime = x + sin(angle - wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_2_yprime = y - cos(angle - wind_triangle_tail_angle)*wind_triangle_tail_len;
    uint16_t tail_mid_xprime = x + sin(angle)*wind_triangle_tail_len/2;
    uint16_t tail_mid_yprime = y - cos(angle)*wind_triangle_tail_len/2;
    
    u8g2.drawTriangle(tip_xprime, tip_yprime, tail_1_xprime, tail_1_yprime, tail_mid_xprime, tail_mid_yprime);
    u8g2.drawTriangle(tip_xprime, tip_yprime, tail_2_xprime, tail_2_yprime, tail_mid_xprime, tail_mid_yprime);
    u8g2.drawLine(tip_xprime, tip_yprime, tail_1_xprime, tail_1_yprime);
    u8g2.drawLine(tail_mid_xprime, tail_mid_yprime, tail_2_xprime, tail_2_yprime);
}



// Draw polygons 
// by default, u8g2 lib supports polygons to 6 points, but only exposes u8g2_DrawTriangle
// Max polygon point size can be increased from 6 in the u8g2 library, but we'll try to write display code that doesn't need library modifications
/*
void u8g2_DrawPoly(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  u8g2_DrawPoly(x0, y0, x1, y1, x2, y2, false, false, false, false, false, false);  
}

void u8g2_DrawPoly(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3) {
  u8g2_DrawPoly(x0, y0, x1, y1, x2, y2, x3, y3, false, false, false, false);  
}

void u8g2_DrawPoly(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t x4, uint16_t y4) {
  u8g2_DrawPoly(x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, false, false);  
}

void u8g2_DrawPoly(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t x4, uint16_t y4, uint16_t x5, uint16_t y5) {
  u8g2_ClearPolygonXY();
  u8g2_AddPolygonXY(u8g2, x0, y0);
  u8g2_AddPolygonXY(u8g2, x1, y1);
  u8g2_AddPolygonXY(u8g2, x2, y2);
  if(x3) {
    u8g2_AddPolygonXY(u8g2, x3, y3);
    if(x4) {
      u8g2_AddPolygonXY(u8g2, x4, y4);
      if(x5) {
        u8g2_AddPolygonXY(u8g2, x5, y5);  
      }
    }
  }
  u8g2_DrawPolygon(u8g2);
}
*/


//void rotate_points()

/* not needed anymore:

// Initialize the GRAPHIC LCD
// used for writing block data to test bmp display images ... won't save in final version
void GLCD_init(void)
{
  pinMode(GLCD_RS, OUTPUT);
	digitalWrite(GLCD_RESET, LOW); 
  delay(100);
  digitalWrite(GLCD_RESET, HIGH);  

	GLCD_inst(0b11100010); //Reset
  delay(20);
  GLCD_inst(0b10101111); //Enable
  delay(20);
  //GLCD_inst(0b10000001); //set Contrast
  //GLCD_inst(0b00001111); //contrast value
  delay(20);  
  GLCD_inst(0b00000000);  //Column address LSB ->0
  delay(20);
  GLCD_inst(0b00010000);  //Column address MSB ->0
  delay(20);
  GLCD_inst(0b10110000);  //Page address ->0
  delay(20);
  GLCD_inst(0b10100110);  //Set inverse display->NO
  delay(20);

  for (int i=0; i<1536; i++){
    GLCD_data(0b00000000);    //clear LCD
  }
}


void display_battery_icon(uint16_t x, uint16_t y, uint8_t pct, bool charging) {

  uint8_t w =  7;
  uint8_t h = 12;  

  u8g2.setDrawColor(1);      
  u8g2.drawFrame(x,y+1,w,h-1);               // main battery box
  u8g2.drawLine(x+w/3, y, x+w-w/3-1, y);     // little +nib tip
  uint16_t fill = (h-2)*pct/100;
  u8g2.drawBox(x+1, y+h-fill-1, w-2, fill);  // fill up to capacity

  if(charging) {                             // draw lightning bolt if charging
    for (int i = 0; i<7; i++) {
      if ( (y+3+i) >= (y+h-fill-1) ) u8g2.setDrawColor(0); //invert lightning bolt where battery level is filled
      u8g2.drawPixel(x+w/2,y+3+i);
      if (i==2) u8g2.drawPixel(x+w/2-1,y+3+i);
      if (i==3) {u8g2.drawPixel(x+w/2-1,y+3+i); u8g2.drawPixel(x+w/2+1,y+3+i);}
      if (i==4) u8g2.drawPixel(x+w/2+1,y+3+i);
    }
    u8g2.setDrawColor(1); //end invert    
  }
}

void display_test_bat_icon(void) {
  uint8_t x = 1;
  uint8_t y = 1;
  uint8_t pct = 0;
  uint8_t w =  7;
  uint8_t h = 12; 
  bool charging = 1;


  u8g2.firstPage();
  do {
    for (int i=0; i<51; i++) {
      u8g2.setDrawColor(1);      
      u8g2.drawFrame(x,y+1,w,h-1);           // main battery box
      u8g2.drawLine(x+w/3, y, x+w-w/3-1, y); // little +nib tip
      uint16_t fill = (h-2)*pct/100;
      u8g2.drawBox(x+1, y+h-fill-1, w-2, fill);  // fill up to capacity

      // draw lightning bolt if charging
      if(charging) {
        for (int i = 0; i<7; i++) {
          if ( (y+3+i) >= (y+h-fill-1) ) u8g2.setDrawColor(0); //invert lightning bolt where battery level is filled
          u8g2.drawPixel(x+w/2,y+3+i);
          if (i==2) u8g2.drawPixel(x+w/2-1,y+3+i);
          if (i==3) {u8g2.drawPixel(x+w/2-1,y+3+i); u8g2.drawPixel(x+w/2+1,y+3+i);}
          if (i==4) u8g2.drawPixel(x+w/2+1,y+3+i);
        }
        u8g2.setDrawColor(1); //end invert
        
      }

      x += 10;
      pct += 2;
      if (x >= 63-w) {
        x = 1;
        y += h+2;
      }  
    }
  } while ( u8g2.nextPage() );
  delay(100);
}


*/

