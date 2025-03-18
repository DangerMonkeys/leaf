#include "PageWarning.h"

#include "display.h"
#include "fonts.h"
#include "power.h"
#include "settings.h"
#include "speaker.h"

enum warning_page_items {
  cursor_warningPage_none,
  cursor_warningPage_decline,
  cursor_warningPage_accept,
};
uint8_t warningPage_cursorMax = 2;

int8_t warningPage_cursorPosition = cursor_warningPage_none;

void warningPage_draw() {
  u8g2.firstPage();
  do {
    // Title
    u8g2.setFont(leaf_icons);
    u8g2.setCursor(2, 12);
    u8g2.print((char)34);
    u8g2.setCursor(84, 12);
    u8g2.print('"');

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(18, 12);
    u8g2.setDrawColor(1);
    u8g2.print("WARNING");

    u8g2.drawHLine(0, 15, 96);

    // Warning Message
    uint8_t line_y = 35;
    uint8_t line_spacing = 11;
    u8g2.setFont(leaf_5x8);

    u8g2.setCursor(0, line_y);
    u8g2.print("  Adventure sports");
    u8g2.setCursor(0, line_y += line_spacing);
    u8g2.print("   are DANGEROUS!");

    u8g2.setCursor(0, line_y += (line_spacing + 5));
    u8g2.print("You accept all risks");
    u8g2.setCursor(0, line_y += line_spacing);
    u8g2.print("of using this product");

    u8g2.setCursor(0, line_y += (line_spacing + 5));
    u8g2.print("  See full terms of");
    u8g2.setCursor(0, line_y += line_spacing);
    u8g2.print("        use at:");

    u8g2.setCursor(0, line_y += (line_spacing + 5));
    u8g2.print(" www.leafvario.com/");
    u8g2.setCursor(0, line_y += line_spacing);
    u8g2.print(" legal/disclaimer/");

    // Accept Decline Options
    u8g2.setFont(leaf_6x12);
    uint8_t box_w = 64;
    uint8_t box_h = 20;
    uint8_t box_x = 16;
    uint8_t box_y = 147;
    uint8_t box_spacing = 5;

    if (warningPage_cursorPosition != cursor_warningPage_accept) {
      u8g2.drawRFrame(box_x, box_y, box_w, box_h, box_h / 4);
    } else {
      u8g2.drawRBox(box_x, box_y, box_w, box_h, box_h / 4);
      u8g2.setDrawColor(0);
    }
    u8g2.setCursor(box_x + 9, box_y + box_h - 4);
    u8g2.print("ACCEPT");
    u8g2.setDrawColor(1);

    if (warningPage_cursorPosition != cursor_warningPage_decline) {
      u8g2.drawRFrame(box_x, box_y + box_h + box_spacing, box_w, box_h, box_h / 4);
    } else {
      u8g2.drawRBox(box_x, box_y + box_h + box_spacing, box_w, box_h, box_h / 4);
      u8g2.setDrawColor(0);
    }
    u8g2.setCursor(box_x + 5, box_y + box_h + box_h + box_spacing - 4);
    u8g2.print("DECLINE");
    u8g2.setDrawColor(1);

  } while (u8g2.nextPage());
}

void warningPage_button(Button button, ButtonState state, uint8_t count) {
  // allow turning off in all states
  if (state == HELD && button == Button::CENTER) {
    power_shutdown();
    return;
  }

  switch (warningPage_cursorPosition) {
    case cursor_warningPage_none:
      switch (button) {
        case Button::UP:
          if (state == RELEASED) {
            warningPage_cursorPosition = cursor_warningPage_decline;
            speaker_playSound(fx_decrease);
          }
          break;
        case Button::DOWN:
          if (state == RELEASED) {
            warningPage_cursorPosition = cursor_warningPage_accept;
            speaker_playSound(fx_increase);
          }
          break;
      }
      break;
    case cursor_warningPage_decline:
      if (state == RELEASED) {
        if (button == Button::UP || button == Button::DOWN) {
          warningPage_cursorPosition = cursor_warningPage_accept;
          speaker_playSound(fx_increase);
        } else if (button == Button::CENTER) {
          power_shutdown();
        }
      }
      break;
    case cursor_warningPage_accept:
      if (state == RELEASED) {
        if (button == Button::UP || button == Button::DOWN) {
          warningPage_cursorPosition = cursor_warningPage_decline;
          speaker_playSound(fx_decrease);
        } else if (button == Button::CENTER) {
          displayDismissWarning();
          speaker_playSound(fx_enter);
        }
      }
      break;
  }
}