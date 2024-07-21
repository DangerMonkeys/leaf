/*
 * buttons.cpp
 *
 * 5-way joystick switch (UP DOWN LEFT RIGHT CENTER)
 * Buttons are active-high, via resistor dividers fed from "SYSPOWER", which is typically 4.4V (supply from Battery Charger under USB power) or Battery voltage (when no USB power is applied)
 * Use internal pull-down resistors on button pins 
 *
 */
#include <Arduino.h>
#include "buttons.h"
#include "power.h"
#include "display.h"
#include "pages.h"
#include "speaker.h"
#include "settings.h"
#include "page_menu_units.h"

//button debouncing 
uint8_t button_debounce_last = NONE;
uint32_t button_debounce_time = 5;    // time in ms for stabilized button state before returning the button press
uint32_t button_time_initial = 0;
uint32_t button_time_elapsed = 0;

//button actions
uint8_t button_last = NONE;
uint8_t button_state = NO_STATE;
uint16_t button_min_hold_time = 800;  // time in ms to count a button as "held down"
uint16_t button_max_hold_time = 3500; // time in ms to start further actions on long-holds

uint16_t button_hold_action_time_initial = 0;
uint16_t button_hold_action_time_elapsed = 0; // counting time between 'action steps' while holding the button
uint16_t button_hold_action_time_limit = 500; // time in ms required between "action steps" while holding the button
uint16_t button_hold_counter = 0;

uint8_t buttons_init(void) {

  //configure pins
  pinMode(BUTTON_PIN_UP, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_DOWN, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_LEFT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_RIGHT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_CENTER, INPUT_PULLDOWN);

  uint8_t button = buttons_inspectPins();
  return button;
}

void buttons_update(void) {
  // TODO: fill this in to handle button pushes with respect to display interface
  uint8_t which_button = buttons_check();
//  uint8_t button_state = buttons_get_state();  //TODO: delete this line probably
  if (display_getPage() == page_menu_units) {
    bool draw_now = units_page.button_event(which_button, buttons_get_state(), buttons_get_hold_count());
    if (draw_now) display_update();
  } else if (display_getPage() == page_charging) {
    switch (which_button) {
      case CENTER:
        if (button_state == HELD && button_hold_counter == 1) {          
          display_clear();          
          display_setPage(page_thermal);
          speaker_playSound(fx_enter);
          power_switchToOnState();
        }
        break;
      case UP:
        switch (button_state) {
          case RELEASED:
            break;
          case HELD:
            power_set_input_current(i500mA);
            speaker_playSound(fx_enter);
            break;
        }
        break;
      case DOWN:
        switch (button_state) {
          case RELEASED:            
            break;
          case HELD:
            power_set_input_current(i100mA);
            speaker_playSound(fx_exit);
            break;
        }
        break;
    }
  } else { // NOT CHARGING PAGE
    switch (which_button) {
      case CENTER:
        switch (button_state) {
          case HELD:
            if (button_hold_counter == 1) {
              display_clear();
              speaker_playSound(fx_exit);
              delay(600);
              power_shutdown();
              while(buttons_inspectPins() == CENTER) {} // freeze here until user lets go of power button
              display_setPage(page_charging);              
            }
            break;
          case RELEASED:
            display_turnPage(page_home);
            break;
        }      
        break;
      case RIGHT:
        if (button_state == RELEASED) {
          display_turnPage(page_next);
          speaker_playSound(fx_increase);
        }
        break;
      case LEFT:
        if (button_state == RELEASED) {
          display_turnPage(page_prev);
          speaker_playSound(fx_decrease);
        }
        break;
      case UP:
        switch (button_state) {
          case RELEASED:
            settings_adjustVolumeVario(1);            
            break;
          case HELD:
            power_set_input_current(i500mA);
            speaker_playSound(fx_enter);
            break;
        }
        break;
      case DOWN:
        switch (button_state) {
          case RELEASED:
            settings_adjustVolumeVario(-1);            
            break;
          case HELD:
            power_set_input_current(i100mA);
            speaker_playSound(fx_exit);
            break;
        }
        break;
    }
  }
}

uint8_t buttons_get_state(void) {
  return button_state;
}

uint16_t buttons_get_hold_count(void) {
  return button_hold_counter;
}




// the recurring call to see if user is pressing buttons.  Handles debounce and button state changes
uint8_t buttons_check(void) {

  uint8_t button = buttons_debounce(buttons_inspectPins());  // check if we have a button press in a stable state
  
  //Serial.print("debounced: ");
  //Serial.println(button);

  // reset and exit if bouncing
  if (button == BOUNCE) {
    button_state = NO_STATE;
    button_hold_counter = 0;
    return NONE;
  }

  // if we have a state change (low to high or high to low)
  if (button != button_last) { 

    button_hold_counter = 0;

    if (button != NONE) {
      button_state = PRESSED;       
    } else {      
      button_state = RELEASED;
      button = button_last;       // we are presently seeing "NONE", which is the release of the previously pressed/held button, so grab that previous button            
    }
  // otherwise we have a non-state change (button is held)
  } else if (button != NONE) {
    if (button_time_elapsed >= button_max_hold_time) {
      button_state = HELD_LONG;
    } else if (button_time_elapsed >= button_min_hold_time) {
      button_state = HELD;
    } else {
      button_state = NO_STATE;
    }   
  } else {
    button_state = NO_STATE;
  }

  // only "act" on a held button every ~500ms (button_hold_action_time_limit)
  if (button_state == HELD || button_state == HELD_LONG) {
    button_hold_action_time_elapsed = millis() - button_hold_action_time_initial;
    if (button_hold_action_time_elapsed < button_hold_action_time_limit) {
      button_state = NO_STATE;
    } else {
      button_hold_counter++;
      button_hold_action_time_elapsed = 0;
      button_hold_action_time_initial = millis();    
    }
  }

/*
Serial.print("button: ");
Serial.print(button);
Serial.print(" state: ");
Serial.print(button_state);
Serial.print(" count: ");
Serial.println(button_hold_counter);
*/



  if(button_state != NO_STATE) {
    switch(button) {
      case CENTER:
        Serial.print("button: CENTER");
        break;
      case LEFT:
        Serial.print("button: LEFT  ");
        break;
      case RIGHT:
        Serial.print("button: RIGHT ");
        break;
      case UP:
        Serial.print("button: UP    ");
        break;
      case DOWN:
        Serial.print("button: DOWN  ");
        break;
      case NONE: 
        Serial.print("button: NONE  ");     
        break;
    }

    switch(button_state) {
      case PRESSED:
        Serial.print(" state: PRESSED  ");
        break;
      case RELEASED:
        Serial.print(" state: RELEASED ");
        break;
      case HELD:
        Serial.print(" state: HELD     ");
        break;
      case HELD_LONG:
        Serial.print(" state: HELD_LONG");
    }

    Serial.print(" hold count: ");
    Serial.println(button_hold_counter);   
  } 


  //save button to track for next time.
  // ..if state is RELEASED, then button_last should be 'NONE', since we're seeing the falling edge of the previous button press
  if (button_state == RELEASED) {
    button_last = NONE;
  } else {
    button_last = button;
  }
  return button;
}

// check the state of the button hardware pins (this is pulled out as a separate function so we can use this for a one-time check at startup)
uint8_t buttons_inspectPins(void) {
  uint8_t button = NONE;
  if      (digitalRead(BUTTON_PIN_CENTER) == HIGH) button = CENTER;
  else if (digitalRead(BUTTON_PIN_DOWN)   == HIGH) button = DOWN;
  else if (digitalRead(BUTTON_PIN_LEFT)   == HIGH) button = LEFT;
  else if (digitalRead(BUTTON_PIN_RIGHT)  == HIGH) button = RIGHT;
  else if (digitalRead(BUTTON_PIN_UP)     == HIGH) button = UP;   
  return button;
}

// check for a minimal stable time before asserting that a button has been pressed or released
uint8_t buttons_debounce(uint8_t button) {  
  if (button != button_debounce_last) {                   // if this is a new button state
    button_time_initial = millis();                       // capture the initial start time
    button_time_elapsed = 0;                              // and reset the elapsed time
    button_debounce_last = button;
  } else {                                                // this is the same button as last time, so calculate the duration of the press
    button_time_elapsed = millis() - button_time_initial; // (the roll-over modulus math works on this)
    if (button_time_elapsed >= button_debounce_time) {        
      return button;
    }
  }
  return BOUNCE;
}


