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
          u8g2.print(char('I' + settings.vario_volume));
          u8g2.setFont(leaf_6x12);
          break;
        case cursor_vario_tones:
          u8g2.print(' ');
          if (settings.vario_tones)
            u8g2.print(char(138));
          else
            u8g2.print(char(139));
          break;
        case cursor_vario_quietmode:
          u8g2.print(' ');
          if (settings.vario_quietMode)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;

        case cursor_vario_sensitive:
          u8g2.print(' ');
          u8g2.print(settings.vario_sensitivity);
          break;
        case cursor_vario_climbavg:
          u8g2.print(' ');
          u8g2.print(settings.vario_climbAvg);
          break;

        case cursor_vario_climbstart:
          if (settings.units_climb) {
            u8g2.print(' ');
            u8g2.print(settings.vario_climbStart * 2);  // cm/s->fpm
          } else {
            u8g2.print(float(settings.vario_climbStart) / 100, 2);  // cm/s->m/s
          }
          break;
        case cursor_vario_liftyair:
          if (settings.vario_liftyAir == 0) {
            u8g2.print("OFF");
          } else if (settings.units_climb) {
            u8g2.print(settings.vario_liftyAir * 20);  // 10cm/s->fpm
          } else {
            u8g2.print(float(settings.vario_liftyAir) / 10, 1);  // 10cm/s->m/s
          }
          break;
        case cursor_vario_sinkalarm:
          if (settings.vario_sinkAlarm == 0) {
            u8g2.print("OFF");
          } else {
            // now print the value
            if (settings.units_climb) {
              // handle the extra digit required if we hit -1000fpm or more
              if (settings.vario_sinkAlarm <= -5) {
                u8g2.setCursor(u8g2.getCursorX() - 7,
                               u8g2.getCursorY());  // scootch over to make room

                // and draw a bigger selection box to fit this one if cursor is here
                if (cursor_position == cursor_vario_sinkalarm) {
                  u8g2.setDrawColor(1);
                  u8g2.drawRBox(setting_choice_x - 10, menu_items_y[cursor_position] - 14, 38, 16,
                                2);
                  u8g2.setDrawColor(0);
                }
              }

              // now print the value as usual
              u8g2.print(settings.vario_sinkAlarm * 200);  // m/s->fpm
            } else {
              u8g2.print(float(settings.vario_sinkAlarm), 1);  // m/s->m/s
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
      settings.adjustVolumeVario(dir);
      break;
    case cursor_vario_quietmode:
      if (state == RELEASED) settings.toggleBoolOnOff(&settings.vario_quietMode);
      break;
    case cursor_vario_sensitive:
      if (state == RELEASED) settings.adjustVarioAverage(dir);
      break;
    case cursor_vario_tones:
      if (state == RELEASED) settings.toggleBoolNeutral(&settings.vario_tones);
      break;
    case cursor_vario_liftyair:
      if (state == RELEASED) settings.adjustLiftyAir(dir);
      break;
    case cursor_vario_climbavg:
      if (state == RELEASED) settings.adjustClimbAverage(dir);
      break;
    case cursor_vario_climbstart:
      if (state == RELEASED) settings.adjustClimbStart(dir);
      break;
    case cursor_vario_sinkalarm:
      if (state == RELEASED) settings.adjustSinkAlarm(dir);
      break;
    case cursor_vario_back:
      if (state == RELEASED) {
        speaker_playSound(fx_cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
        speaker_playSound(fx_exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
  }
}