#include <Arduino.h>
#include "PageMenuDisplay.h"
#include "pages.h"
#include "buttons.h"
#include "display.h"
#include "fonts.h"
#include "settings.h"
#include "speaker.h"


enum display_menu_items {
  cursor_display_back,
  cursor_display_contrast,
  cursor_display_AA,
  cursor_display_BB,
  cursor_display_CC,
  cursor_display_DD,
  cursor_display_EE,
  cursor_display_FF

};


void DisplayMenuPage::draw() {
  u8g2.firstPage();
  do { 
    // Title(s) 
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 12);
    u8g2.setDrawColor(1);
    u8g2.print("DISPLAY");
    u8g2.drawHLine(0, 15, 64);

  // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 44;    
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105, 120, 135};

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
        case cursor_display_contrast:
          u8g2.print(CONTRAST);
          break;
        case cursor_display_AA:
          u8g2.print("AA");          
          break;
        case cursor_display_BB:
          u8g2.print("BB");          
          break;
        case cursor_display_CC:
          u8g2.print("CC");          
          break;
        case cursor_display_DD:
          u8g2.print("DD");          
          break;
        case cursor_display_EE:
          u8g2.print("EE");          
          break;
        case cursor_display_FF:
          u8g2.print("FF");          
          break;
        case cursor_display_back:
          u8g2.print((char)124);
          break;        
      }
    u8g2.setDrawColor(1);
    }
  } while ( u8g2.nextPage() ); 
}

void DisplayMenuPage::setting_change(buttons dir, button_states state, uint8_t count) {
  switch (cursor_position) {
    case cursor_display_contrast:
      if (state == RELEASED && dir != 0) settings_adjustContrast(dir);
      else if (state == HELD && dir == 0) settings_adjustContrast(dir);
      break;
    case cursor_display_AA:

      break;
    case cursor_display_BB:

      break;
    case cursor_display_CC:

      break;
    case cursor_display_DD:

      break;
    case cursor_display_EE:

      break;
    case cursor_display_FF:

      break;
    case cursor_display_back:
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