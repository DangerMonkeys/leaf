#include "PageMenuVario.h"

#include <Arduino.h>

#include "baro.h"
#include "buttons.h"
#include "display.h"
#include "displayFields.h"
#include "fonts.h"
#include "gps.h"
#include "pages.h"
#include "settings.h"
#include "speaker.h"

enum vario_menu_items {
  cursor_vario_back,
  cursor_vario_volume,
  cursor_vario_tones,
  cursor_vario_quietmode,

  cursor_vario_sensitive,
  cursor_vario_climbavg,

  cursor_vario_climbstart,
  cursor_vario_liftyair,
  cursor_vario_sinkalarm,
};

void VarioMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title(s)
    display_menuTitle("VARIO");

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 68;
    uint8_t menu_items_y[] = {190, 40, 55, 70, 95, 110, 135, 150, 165};

    // first draw cursor selection box
    u8g2.drawRBox(setting_choice_x - 2, menu_items_y[cursor_position] - 14, 30, 16, 2);

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      u8g2.print(labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      if (i == cursor_position)
        u8g2.setDrawColor(0);
      else
        u8g2.setDrawColor(1);
      switch (i) {
        case cursor_vario_volume:
          u8g2.print(' ');
          u8g2.setFont(leaf_icons);
          u8g2.print(char('I' + VOLUME_VARIO));
          u8g2.setFont(leaf_6x12);
          break;
        case cursor_vario_tones:
          u8g2.print(' ');
          if (VARIO_TONES)
            u8g2.print(char(138));
          else
            u8g2.print(char(139));
          break;
        case cursor_vario_quietmode:
          u8g2.print(' ');
          if (QUIET_MODE)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;

        case cursor_vario_sensitive:
          u8g2.print(' ');
          u8g2.print(VARIO_SENSE);
          break;
        case cursor_vario_climbavg:
          u8g2.print(' ');
          u8g2.print(CLIMB_AVERAGE);
          break;

        case cursor_vario_climbstart:
          if (UNITS_climb) {
            u8g2.print(' ');
            u8g2.print(CLIMB_START * 2);  // cm/s->fpm
          } else {
            u8g2.print(float(CLIMB_START) / 100, 2);  // cm/s->m/s
          }
          break;
        case cursor_vario_liftyair:
          if (LIFTY_AIR == 0) {
            u8g2.print("OFF");
          } else if (UNITS_climb) {
            u8g2.print(LIFTY_AIR * 20);  // 10cm/s->fpm
          } else {
            u8g2.print(float(LIFTY_AIR) / 10, 1);  // 10cm/s->m/s
          }
          break;
        case cursor_vario_sinkalarm:
          if (SINK_ALARM == 0) {
            u8g2.print("OFF");
          } else {


            // now print the value
            if (UNITS_climb) {
              // handle the extra digit required if we hit -1000fpm or more
              if (SINK_ALARM <= -5) {
                u8g2.setCursor(u8g2.getCursorX() - 7, u8g2.getCursorY()); // scootch over to make room 

                // and draw a bigger selection box to fit this one if cursor is here
                if (cursor_position == cursor_vario_sinkalarm) {
                  u8g2.setDrawColor(1);
                  u8g2.drawRBox(setting_choice_x - 10, menu_items_y[cursor_position] - 14, 38, 16, 2);
                  u8g2.setDrawColor(0);
                }                
              }
              
              // now print the value as usual
              u8g2.print(SINK_ALARM * 200);  // m/s->fpm
            } else {
              u8g2.print(float(SINK_ALARM), 1);  // m/s->m/s
            }
          }
          break;

        case cursor_vario_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void VarioMenuPage::setting_change(Button dir, ButtonState state, uint8_t count) {
  switch (cursor_position) {
    case cursor_vario_volume:
      if (state != RELEASED) return;
      settings_adjustVolumeVario(dir);
      break;
    case cursor_vario_quietmode:
      if (state == RELEASED) settings_toggleBoolOnOff(&QUIET_MODE);
      break;
    case cursor_vario_sensitive:
      if (state == RELEASED) settings_adjustVarioAverage(dir);
      break;
    case cursor_vario_tones:
      if (state == RELEASED) settings_toggleBoolNeutral(&VARIO_TONES);
      break;
    case cursor_vario_liftyair:
      if (state == RELEASED) settings_adjustLiftyAir(dir);
      break;
    case cursor_vario_climbavg:
      if (state == RELEASED) settings_adjustClimbAverage(dir);
      break;
    case cursor_vario_climbstart:
      if (state == RELEASED) settings_adjustClimbStart(dir);
      break;
    case cursor_vario_sinkalarm:
      if (state == RELEASED) settings_adjustSinkAlarm(dir);
      break;
    case cursor_vario_back:
      if (state == RELEASED) {
        speaker_playSound(fx_cancel);
        settings_save();
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
        speaker_playSound(fx_exit);
        settings_save();
        mainMenuPage.quitMenu();
      }
      break;
  }
}