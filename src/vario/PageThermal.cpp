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


enum thermal_page_items { 
  cursor_thermalPage_none,
	cursor_thermalPage_alt1,
	//cursor_thermalPage_alt2,
	//cursor_thermalPage_userField1,
	//cursor_thermalPage_userField2,
	cursor_thermalPage_timer
};
uint8_t cursor_max = 2;

int8_t cursor_position = cursor_thermalPage_none;
uint8_t cursor_timeCount = 0;	// count up for every page_draw, and if no button is pressed, then reset cursor to "none" after the timeOut value is reached.
uint8_t cursor_timeOut = 8;	// after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

void thermalPage_draw() {

	// if cursor is selecting something, count toward the timeOut value before we reset cursor
	if (cursor_position != cursor_thermalPage_none && cursor_timeCount++ >= cursor_timeOut) {
		cursor_position = cursor_thermalPage_none;
		cursor_timeCount = 0;		
	}

  u8g2.firstPage();
  do { 

		// Header Info ****************************************************
			// clock time
			u8g2.setFont(leaf_6x10);
			display_clockTime(0, 10, false);

			//heading
			u8g2.setFont(leaf_7x10);
			display_heading(40, 10, true);

			//battery 
			display_battIcon(89, 13, true);

			//speed
			u8g2.setFont(leaf_6x12);
			display_speed(0,24);
			u8g2.setFont(leaf_5h);
			u8g2.setCursor(u8g2.getCursorX()+2, 24);
			u8g2.print("MPH");
			
			
		// Main Info ****************************************************
			uint8_t topOfFrame = 30;
			uint8_t graphBoxHeight = 40;
			uint8_t varioBarWidth = 20;
			uint8_t varioBarHeight = 141;

			// Graph Box
			u8g2.drawFrame(varioBarWidth-1, topOfFrame, 96-varioBarWidth+1, graphBoxHeight);

			// Vario Bar
			display_varioBar(topOfFrame, varioBarHeight, varioBarWidth, baro.climbRateFiltered);

			// alt
			display_alt_type(22, 89, leaf_8x14, THMPG_ALT_TYP);
			
			// altselection box
			if (cursor_position == cursor_thermalPage_alt1) {
				display_selectionBox(21, 73, 96-21, 18, 6);
			}

			//climb rate
			display_climbRatePointerBox(20, 92, 76, 17, 6);     // x, y, w, h, triangle size
			display_climbRate(20, 108, leaf_8x14, baro.climbRateFiltered);
			


			display_altAboveLaunch(24, 129, baro.altAboveLaunch);
		
			
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
			display_flightTimer(51, 191, 0, (cursor_position == cursor_thermalPage_timer));			

			// Bottom Status Icons	
				// SD Card Present
				char SDicon = 60;
				if(!SDcard_present()) SDicon = 61;
				u8g2.setCursor(12, 191);
				u8g2.setFont(leaf_icons);
				u8g2.print((char)SDicon);
			



		// Testing
		//u8g2.drawBox(8,174, 16, 16);



    
  } while ( u8g2.nextPage() ); 
  
}

void cursor_move(buttons button) {
	if (button == UP) {
		cursor_position--;
		if (cursor_position < 0) cursor_position = cursor_max;
	}
	if (button == DOWN) {
		cursor_position++;
  	if (cursor_position > cursor_max) cursor_position = 0;
	}
}


void thermalPage_button(buttons button, button_states state, uint8_t count) {

	// reset cursor time out count if a button is pushed
	cursor_timeCount = 0;

	switch (cursor_position) {
		case cursor_thermalPage_none:
			switch(button) {
				case UP:
				case DOWN:
					if (state == RELEASED) cursor_move(button);     					
					break;
				case RIGHT:
					if (state == RELEASED) {
						display_turnPage(page_next);
						speaker_playSound(fx_increase);
					}
					break;
				case LEFT:
					if (state == RELEASED) {
						display_turnPage(page_prev);
						speaker_playSound(fx_decrease);
					}
					break;
				case CENTER:
					break;
			}
			break;
		case cursor_thermalPage_alt1:
			switch(button) {
				case UP:
				case DOWN:
					if (state == RELEASED) cursor_move(button);     					
					break;
				case LEFT:
					if (NAVPG_ALT_TYP == altType_MSL && (state == PRESSED || state == HELD || state == HELD_LONG)) {
          	baro_adjustAltSetting(-1, count);
          	speaker_playSound(fx_neutral);
        	}
					break;
				case RIGHT:
					if (NAVPG_ALT_TYP == altType_MSL && (state == PRESSED || state == HELD || state == HELD_LONG)) {
          	baro_adjustAltSetting(1, count);
          	speaker_playSound(fx_neutral);
        	}
					break;
				case CENTER:
					if (state == RELEASED) settings_adjustDisplayField_thermalPage_alt(1);
					else if (state == HELD && count == 1 && THMPG_ALT_TYP == altType_MSL)  {
						if (settings_matchGPSAlt()) { // successful adjustment of altimeter setting to match GPS altitude
          		speaker_playSound(fx_enter);  
              cursor_position = cursor_thermalPage_none;
        		} else {                      // unsuccessful 
          	speaker_playSound(fx_cancel);
        		}
					}
					break;
			}
			break;
		/* case cursor_thermalPage_alt2:
			switch(button) {
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
			}
			break;
		case cursor_thermalPage_userField1:
			switch(button) {
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
			}
			break;
		case cursor_thermalPage_userField2:
			switch(button) {
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
			}
			break;
			*/
		case cursor_thermalPage_timer:
			switch(button) {
				case UP:
				case DOWN:
					if (state == RELEASED) cursor_move(button);     					
					break;
				case LEFT:
					break;
				case RIGHT:
					break;
				case CENTER:
						if (state == RELEASED) {
							flightTimer_toggle();
							cursor_position = cursor_thermalPage_none;
						}	else if (state == HELD) {
							flightTimer_reset();
							cursor_position = cursor_thermalPage_none;
						}
						
					break;
			}
			break;
	}
}