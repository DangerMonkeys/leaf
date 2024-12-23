/*
 * display.cpp
 *
 *
 */
#include <Arduino.h>
#include <U8g2lib.h>

#include "display.h"
#include "displayFields.h"
#include "display_tests.h"
#include "fonts.h"

#include "pages.h"
#include "PageThermal.h"
#include "PageThermalSimple.h"
#include "PageNavigate.h"

#include "Leaf_SPI.h"
#include "gps.h"
#include "gpx.h"
#include "baro.h"
#include "power.h"
#include "log.h"
#include "settings.h"
#include "speaker.h"
#include "power.h"
#include "SDcard.h"
#include "menu_page.h"
#include "version.h"

//#define GLCD_RS LCD_RS
//#define GLCD_RESET LCD_RESET

// Display Testing Temp Vars
float wind_angle = 1.57;
char seconds = 0;
char minutes = 0;
char hours = 0;
uint16_t heading = 0;
char string_heading[] = " WNW ";

// Leaf V3.2.2
#ifdef DISPLAY_JUNE
  U8G2_ST75256_WO256X128_F_4W_HW_SPI u8g2(U8G2_R3,  /* cs=*/ SPI_SS_LCD, /* dc=*/ LCD_RS, /* reset=*/ LCD_RESET);  // June Huang
#endif
#ifdef DISPLAY_ALICE
  U8G2_ST75256_JLX19296_F_4W_HW_SPI u8g2(U8G2_R1, /* cs=*/ SPI_SS_LCD, /* dc=*/ LCD_RS, /* reset=*/ LCD_RESET); // Alice Green HW
#endif
#ifdef DISPLAY_ALICE_SMALL
  U8G2_ST7539_192X64_F_4W_HW_SPI u8g2(U8G2_R3, SPI_SS_LCD, LCD_RS, LCD_RESET);                                  // 192x64 original
#endif

int8_t display_page = page_thermalSimple;
uint8_t display_page_prior = page_thermalSimple; // track the page we used to be on, so we can "go back" if needed (like cancelling out of a menu heirarchy)

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
  #ifdef DISPLAY_JUNE
    u8g2.setContrast(contrast);   // JUne Huang
  #endif
  #ifdef DISPLAY_ALICE
    u8g2.setContrast(contrast/2+25);   // Alice Green
  #endif
  #ifdef DISPLAY_ALICE_SMALL
    u8g2.setContrast(contrast/3+15);   // 192x64 original
  #endif
}

void display_turnPage(uint8_t action) {
  uint8_t tempPage = display_page;
  
  switch (action) {
    case page_home: 
      display_page = page_thermalSimple;
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

// Draws the current page
// Will first display charging screen if charging,
// Then will display any current modal pages before
// falling back to the current 
void display_update() {
  if (display_page == page_charging) {
    display_page_charging();
    return;
  }

  auto modalPage = mainMenuPage.get_modal_page();
  if(modalPage != NULL) {
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
    case page_sats:
      display_page_satellites();
      break;
    case page_nav:
      navigatePage_draw();
      break;
    case page_menu:
      mainMenuPage.draw();
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





/*********************************************************************************
**   CHARGING PAGE    ************************************************************
*********************************************************************************/
void display_page_charging() {
  
  u8g2.firstPage();
  do { 

    display_batt_charging_fullscreen();

    // Display the current version
    u8g2.setCursor(0, 16);
    u8g2.setFont(leaf_5h);
    u8g2.print("v");
    u8g2.print(VERSION);

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(34, 14);
    if      (power_getInputCurrent() == i100mA)  u8g2.print("100mA");
    else if (power_getInputCurrent() == i500mA)  u8g2.print("500mA");
    else if (power_getInputCurrent() == iMax)  u8g2.print("810mA");
    else if (power_getInputCurrent() == iStandby)  u8g2.print(" OFF");
    
    // Bottom Status Icons	
      // SD Card Present
      char SDicon = 60;
      if(!SDcard_present()) SDicon = 61;
      u8g2.setCursor(12, 191);
      u8g2.setFont(leaf_icons);
      u8g2.print((char)SDicon);

  } while ( u8g2.nextPage() ); 
  
}




/*********************************************************************************
**    SATELLITES TEST PAGE     ***************************************************
*********************************************************************************/

// draw satellite constellation starting in upper left x, y and box size (width = height)
void display_page_satellites() {
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
        u8g2.print(gps.altitude.meters()*3.028084);
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

        u8g2.setCursor(x, y+=15);
        u8g2.print(battMV);
        u8g2.setCursor(x, y+=15);
        u8g2.print(battADC);

        // glide ratio debugging
        u8g2.setCursor(0,73);
        u8g2.print("AvCl");
        u8g2.print(baro.climbRateAverage);
        u8g2.setCursor(0,86);
        u8g2.print("Clmb:");
        u8g2.print(baro.climbRateFiltered);
        u8g2.setCursor(0,99);
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


        u8g2.setCursor(65,110);
        u8g2.print(baro.altimeterSetting);


    gpsMenuPage.drawConstellation(0,100,63);
    

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

