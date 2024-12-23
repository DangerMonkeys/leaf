#include <Arduino.h>
#include <U8g2lib.h>

#include "display.h"
#include "fonts.h"
#include "displayFields.h"
#include "buttons.h"
#include "speaker.h"

#include "baro.h"
#include "power.h"
#include "tempRH.h"
#include "SDcard.h"
#include "log.h"
#include "IMU.h"
#include "gps.h"
#include "settings.h"


enum thermalSimple_page_items { 
  cursor_thermalSimplePage_none,
	cursor_thermalSimplePage_alt1,
	//cursor_thermalSimplePage_alt2,
	//cursor_thermalSimplePage_userField1,
	//cursor_thermalSimplePage_userField2,
	cursor_thermalSimplePage_timer
};
uint8_t thermalSimple_page_cursor_max = 2;

int8_t thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
uint8_t thermalSimple_page_cursor_timeCount = 0;	// count up for every page_draw, and if no button is pressed, then reset cursor to "none" after the timeOut value is reached.
uint8_t thermalSimple_page_cursor_timeOut = 8;	// after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

float test_wind_angle = 0;

void thermalSimplePage_draw() {

	// if cursor is selecting something, count toward the timeOut value before we reset cursor
	if (thermalSimple_page_cursor_position != cursor_thermalSimplePage_none && thermalSimple_page_cursor_timeCount++ >= thermalSimple_page_cursor_timeOut) {
		thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
		thermalSimple_page_cursor_timeCount = 0;		
	}

  u8g2.firstPage();
  do { 

		// Status Icons and Info ****************************************************
			// clock time
			u8g2.setFont(leaf_6x10);
			display_clockTime(0, 10, false);
			
			//battery 
			display_battIcon(0, 192, true);

			// SD Card Present
			char SDicon = 60;
			if(!SDcard_present()) SDicon = 61;
			u8g2.setCursor(10, 192);
			u8g2.setFont(leaf_icons);
			u8g2.print((char)SDicon);
			


		// Heading, Speed and Wind *******************************************

			//heading
			u8g2.setFont(leaf_7x10);
			display_heading(37, 10, true);

			//speed
			u8g2.setFont(leaf_8x14);
			display_speed(70,14);
			u8g2.setFont(leaf_5h);			
			u8g2.setCursor(82, 21);
			if (UNITS_speed) u8g2.print("MPH");
			else u8g2.print("KPH");

			//wind
			u8g2.drawDisc(49, 25, 12);
			u8g2.setDrawColor(0);
			display_windSock(49, 25, 10, test_wind_angle);//0.78);
			u8g2.setDrawColor(1);

			test_wind_angle += .1;
			if (test_wind_angle > 2*PI) 
				test_wind_angle -= (2*PI);

		// Main Info ****************************************************
			uint8_t topOfFrame = 30;
			uint8_t graphBoxHeight = 40;
			uint8_t varioBarWidth = 20;
			uint8_t varioBarHeight = 141;
			
			// Vario Bar
			display_varioBar(topOfFrame, varioBarHeight, varioBarWidth, baro.climbRateFiltered);

		
			//Altitude			
        uint8_t alt_y = 41;
				//Altitude header labels
				u8g2.setFont(leaf_labels);
				u8g2.setCursor(varioBarWidth+44, alt_y);		
				print_alt_label(THMPG_ALT_TYP);
				u8g2.setCursor(96-9, alt_y);		
				if (UNITS_alt) u8g2.print("ft");
				else u8g2.print("m");

				// Alt value
				display_alt_type(varioBarWidth+6, alt_y+21, leaf_21h, THMPG_ALT_TYP);
				
				// if selected, draw the box around it
				if (thermalSimple_page_cursor_position == cursor_thermalSimplePage_alt1) {
					display_selectionBox(varioBarWidth+1, alt_y-1, 96-(varioBarWidth+1), 24, 7);
				}

			//Alt 2 (user alt field; above launch, etc)
			display_altAboveLaunch(varioBarWidth+4, 84, baro.altAboveLaunch);

			//Climb 
			display_climbRatePointerBox(20, 87, 76, 27, 13);     // x, y, w, h, triangle size
			display_climbRate(20, 111, leaf_21h, baro.climbRateFiltered);
			u8g2.setDrawColor(0);
			if (UNITS_climb) u8g2.print("f");
			else u8g2.print('m');
			u8g2.setDrawColor(1);
			


/*      if (selected) {
        u8g2.drawRFrame(cursor_x, cursor_y-16, 96-cursor_x, 18, 3);
      }
			*/
			
		// User Fields ****************************************************
			uint8_t userFieldsTop = 136;
			uint8_t userFieldsHeight = 17;
			uint8_t userFieldsMid = userFieldsTop + userFieldsHeight;
			uint8_t userFieldsBottom = userFieldsMid + userFieldsHeight;
			uint8_t userSecondColumn = varioBarWidth/2+48;

			u8g2.drawHLine(varioBarWidth-1, userFieldsTop, 96-varioBarWidth+1);
			u8g2.drawHLine(varioBarWidth-1, userFieldsMid, 96-varioBarWidth+1);
			u8g2.drawHLine(varioBarWidth-1, userFieldsBottom, 96-varioBarWidth+1);
			u8g2.drawVLine(userSecondColumn, userFieldsTop, userFieldsHeight*2);



			display_temp(varioBarWidth+5, userFieldsMid-1, (int16_t)tempRH_getTemp());
			display_humidity(userSecondColumn+3, userFieldsMid-1, (uint8_t)tempRH_getHumidity());
			display_accel(varioBarWidth+5, userFieldsBottom-1, IMU_getAccel());
			display_glide(userSecondColumn+3, userFieldsBottom-1, gps_getGlideRatio());


    // Footer Info ****************************************************
		
			//flight timer (display selection box if selected)
			display_flightTimer(51, 191, 0, (thermalSimple_page_cursor_position == cursor_thermalSimplePage_timer));			

			// Bottom Status Icons	
				
			



		// Testing
		//u8g2.drawBox(8,174, 16, 16);



    
  } while ( u8g2.nextPage() ); 
  
}

void thermalSimple_page_cursor_move(Button button) {
	if (button == Button::UP) {
		thermalSimple_page_cursor_position--;
		if (thermalSimple_page_cursor_position < 0) thermalSimple_page_cursor_position = thermalSimple_page_cursor_max;
	}
	if (button == Button::DOWN) {
		thermalSimple_page_cursor_position++;
  	if (thermalSimple_page_cursor_position > thermalSimple_page_cursor_max) thermalSimple_page_cursor_position = 0;
	}
}


void thermalSimplePage_button(Button button, ButtonState state, uint8_t count) {

	// reset cursor time out count if a button is pushed
	thermalSimple_page_cursor_timeCount = 0;

	switch (thermalSimple_page_cursor_position) {
		case cursor_thermalSimplePage_none:
			switch(button) {
				case Button::UP:
				case Button::DOWN:
					if (state == RELEASED) thermalSimple_page_cursor_move(button);     					
					break;
				case Button::RIGHT:
					if (state == RELEASED) {
						display_turnPage(page_next);
						speaker_playSound(fx_increase);
					}
					break;
				case Button::LEFT:
					if (state == RELEASED) {
						display_turnPage(page_prev);
						speaker_playSound(fx_decrease);
					}
					break;
				case Button::CENTER:
					break;
			}
			break;
		case cursor_thermalSimplePage_alt1:
			switch(button) {
				case Button::UP:
				case Button::DOWN:
					if (state == RELEASED) thermalSimple_page_cursor_move(button);     					
					break;
				case Button::LEFT:
					if (NAVPG_ALT_TYP == altType_MSL && (state == PRESSED || state == HELD || state == HELD_LONG)) {
          	baro_adjustAltSetting(Button::LEFT, count);
          	speaker_playSound(fx_neutral);
        	}
					break;
				case Button::RIGHT:
					if (NAVPG_ALT_TYP == altType_MSL && (state == PRESSED || state == HELD || state == HELD_LONG)) {
          	baro_adjustAltSetting(Button::RIGHT, count);
          	speaker_playSound(fx_neutral);
        	}
					break;
				case Button::CENTER:
					if (state == RELEASED) settings_adjustDisplayField_thermalPage_alt(Button::CENTER);
					else if (state == HELD && count == 1 && THMPG_ALT_TYP == altType_MSL)  {
						if (settings_matchGPSAlt()) { // successful adjustment of altimeter setting to match GPS altitude
          		speaker_playSound(fx_enter);  
              thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
        		} else {                      // unsuccessful 
          	speaker_playSound(fx_cancel);
        		}
					}
					break;
			}
			break;
		/* case cursor_thermalSimplePage_alt2:
			switch(button) {
				case Button::UP:
					break;
				case Button::DOWN:
					break;
				case Button::LEFT:
					break;
				case Button::RIGHT:
					break;
				case Button::CENTER:
					break;
			}
			break;
		case cursor_thermalSimplePage_userField1:
			switch(button) {
				case Button::UP:
					break;
				case Button::DOWN:
					break;
				case Button::LEFT:
					break;
				case Button::RIGHT:
					break;
				case Button::CENTER:
					break;
			}
			break;
		case cursor_thermalSimplePage_userField2:
			switch(button) {
				case Button::UP:
					break;
				case Button::DOWN:
					break;
				case Button::LEFT:
					break;
				case Button::RIGHT:
					break;
				case Button::CENTER:
					break;
			}
			break;
			*/
		case cursor_thermalSimplePage_timer:
			switch(button) {
				case Button::UP:
				case Button::DOWN:
					if (state == RELEASED) thermalSimple_page_cursor_move(button);     					
					break;
				case Button::LEFT:
					break;
				case Button::RIGHT:
					break;
				case Button::CENTER:
						if (state == RELEASED) {
							flightTimer_toggle();
							thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
						}	else if (state == HELD) {
							flightTimer_reset();
							thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
						}
						
					break;
			}
			break;
	}
}