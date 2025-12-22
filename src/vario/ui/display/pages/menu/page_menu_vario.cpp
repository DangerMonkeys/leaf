#include "ui/display/pages/menu/page_menu_vario.h"

#include <Arduino.h>

#include "instruments/baro.h"
#include "instruments/gps.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum vario_menu_items {
  cursor_vario_back,
  cursor_vario_volume,
  // cursor_vario_tones,
  cursor_vario_quietmode,

  cursor_vario_sensitive,
  // cursor_vario_climbavg,

  cursor_vario_climbstart,
  // cursor_vario_liftyair,
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
    uint8_t menu_items_y[] = {190, 40, 55, 70, 95, 110 /*, 135, 150, 165*/};

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
        /*
        case cursor_vario_tones:
          u8g2.print(' ');
          if (settings.vario_tones)
            u8g2.print(char(138));
          else
            u8g2.print(char(139));
          break;
          */
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
        /*
        case cursor_vario_climbavg:
          u8g2.print(' ');
          u8g2.print(settings.vario_climbAvg);
          break;
        */
        case cursor_vario_climbstart:
          if (settings.units_climb) {
            u8g2.print(' ');
            u8g2.print(settings.vario_climbStart * 2);  // cm/s->fpm
          } else {
            u8g2.print(float(settings.vario_climbStart) / 100, 2);  // cm/s->m/s
          }
          break;
        /*
        case cursor_vario_liftyair:
          if (settings.vario_liftyAir == 0) {
            u8g2.print("OFF");
          } else if (settings.units_climb) {
            u8g2.print(settings.vario_liftyAir * 20);  // 10cm/s->fpm
          } else {
            u8g2.print(float(settings.vario_liftyAir) / 10, 1);  // 10cm/s->m/s
          }
          break;
          */
        case cursor_vario_sinkalarm:
          if (settings.vario_sinkAlarm > -1.0f) {
            u8g2.print("OFF");
          } else {
            // confirm sink alarm setting is in the same units as climb units setting
            if (settings.vario_sinkAlarm_units != settings.units_climb) {
              settings.adjustSinkAlarmUnits(settings.units_climb);
            }

            // now print the value
            if (settings.units_climb) {
              // handle the extra digit required if we hit -1000fpm or more
              if (settings.vario_sinkAlarm <= -1000.0f) {
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
              u8g2.print(float(settings.vario_sinkAlarm), 0);  // fpm
            } else {
              u8g2.print(float(settings.vario_sinkAlarm), 1);  // m/s
            }
          }
          // Print units for climb/sink thresholds
          u8g2.setFont(leaf_labels);
          if (settings.units_climb) {
            u8g2.setCursor(u8g2.getCursorX() - 20, u8g2.getCursorY() + 10);
            u8g2.print("fpm");
          } else {
            u8g2.setCursor(u8g2.getCursorX() - 18, u8g2.getCursorY() + 11);
            u8g2.print("m/s");
          }
          u8g2.setFont(leaf_6x12);
          break;

        case cursor_vario_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void VarioMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_vario_volume:
      if (state != ButtonEvent::CLICKED) return;
      settings.adjustVolumeVario(dir);
      break;
    case cursor_vario_quietmode:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.vario_quietMode);
      break;
    case cursor_vario_sensitive:
      if (state == ButtonEvent::CLICKED) settings.adjustVarioAverage(dir);
      break;
      /*
    case cursor_vario_tones:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolNeutral(&settings.vario_tones);
      break;
    case cursor_vario_liftyair:
      if (state == ButtonEvent::CLICKED) settings.adjustLiftyAir(dir);
      break;
      */
    /*
    case cursor_vario_climbavg:
      if (state == ButtonEvent::CLICKED) settings.adjustClimbAverage(dir);
      break;
    */
    case cursor_vario_climbstart:
      if (state == ButtonEvent::CLICKED) settings.adjustClimbStart(dir);
      break;
    case cursor_vario_sinkalarm:
      if (state == ButtonEvent::CLICKED) settings.adjustSinkAlarm(dir);
      break;
    case cursor_vario_back:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
  }
}