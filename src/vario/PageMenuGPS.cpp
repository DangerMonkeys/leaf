#include <Arduino.h>
#include "PageMenuGPS.h"
#include "pages.h"

#include "buttons.h"
#include "display.h"
#include "fonts.h"
#include "settings.h"
#include "speaker.h"
#include "gps.h"

char string_satNum[] = "00";

enum gps_menu_items { 
  cursor_gps_back,
  cursor_gps_update,
  cursor_gps_aa,
  cursor_gps_bb
};


void GPSMenuPage::draw() {
  u8g2.firstPage();
  do { 
    // Title(s) 
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 12);
    u8g2.setDrawColor(1);
    u8g2.print("GPS");
    u8g2.drawHLine(0, 15, 64);

  // GPS constellation and lat/long
    drawConstellation(0, 20, 63);

  // Menu Items
    u8g2.setFont(leaf_6x12);
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 44;    
    uint8_t menu_items_y[] = {190, 140, 155, 170};

    //first draw cursor selection box
    u8g2.drawRBox(setting_choice_x-2, menu_items_y[cursor_position]-14, 22, 16, 2);
    
    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {      
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      u8g2.print(labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      if (i == cursor_position) u8g2.setDrawColor(0);
      else u8g2.setDrawColor(1);
      switch (i) {
        case cursor_gps_update:
          if (UNITS_alt) u8g2.print("ft");
          else u8g2.print(" m");
          break;
        case cursor_gps_aa:
          if (UNITS_climb) u8g2.print("fpm");
          else u8g2.print("m/s");    
          break;
        case cursor_gps_bb:
          if (UNITS_speed) u8g2.print("mph");
          else u8g2.print("kph");
          break;
        case cursor_gps_back:
          u8g2.print((char)124);
          break;        
      }
    u8g2.setDrawColor(1);
    }
  } while ( u8g2.nextPage() ); 
}


void GPSMenuPage::setting_change(buttons dir, button_states state, uint8_t count) {
  switch (cursor_position) {
    case cursor_gps_update:

      break;
    case cursor_gps_aa:

      break;
    case cursor_gps_bb:

      break;
    case cursor_gps_back:
      if (state == RELEASED) {
        speaker_playSound(fx_cancel);
        settings_save(); 
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
        speaker_playSound(fx_exit);
        settings_save(); 
        mainMenuPage.quitMenu();        
      }      
  }
}


void GPSMenuPage::drawConstellation(uint8_t x, uint8_t y, uint16_t size) {

  // Draw the satellite background
  //u8g2.setDrawColor(0);
  //u8g2.drawBox(x, y, size, size);   // clear the box drawing area
  //u8g2.setDrawColor(1);
  u8g2.drawCircle(x+size/2, y+size/2, size/2);   // the horizon circle
  u8g2.drawCircle(x+size/2, y+size/2, size/4);   // the 45deg elevation circle


  // Draw the satellites
  for (int i = MAX_SATELLITES -1; i>=0; i--) { 
    if (satsDisplay[i].active) {

      // Sat location (on circle display)
      uint16_t radius = (90 - satsDisplay[i].elevation) * size/2 / 90;
      int16_t sat_x = sin(satsDisplay[i].azimuth*PI/180)*radius;
      int16_t sat_y = - cos(satsDisplay[i].azimuth*PI/180)*radius;

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
      if (satsDisplay[i].snr < 20) {                    
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
    
   
  //draw lat long
    //u8g2.setFont(leaf_5h);
    u8g2.drawStr(0, size + y + 10, "Lat:");
    u8g2.setCursor(16+8, size + y + 10);
    u8g2.print(gps.location.lat(), 7);

    u8g2.drawStr(0, size + y + 20, "Lon:");
    u8g2.setCursor(16, size + y + 20);
    u8g2.print(gps.location.lng(), 7);
}

// helpful switch constructors to copy-paste as needed:
/*
switch (button) {
  case UP:
    break;
  case DOWN:
    break;
  case LEFT:
    break;
  case RIGHT:
    break;
  case CENTER:
    break;
*/

/*
switch (state) {
  case RELEASED:
    break;
  case PRESSED:
    break;
  case HELD:
    break;
  case HELD_LONG:
    break;
}
*/