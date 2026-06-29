#include "ui/display/pages/dialogs/page_flight_summary.h"

#include "Arduino.h"
#include "hardware/buttons.h"
#include "logbook/logbook_entry.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/logbook_card.h"

bool PageFlightSummary::showing_ = false;

namespace {
  constexpr uint8_t MENU_INPUT_X = 74;
  constexpr uint8_t MENU_BACK_Y = 190;
  constexpr uint8_t DELETE_HOLD_COUNT = 5;
}  // namespace

PageFlightSummary::PageFlightSummary() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = 0;
}

void PageFlightSummary::draw() {
  u8g2.firstPage();
  do {
    display_menuTitle("SUMMARY");

    u8g2.setFont(leaf_6x12);
    if (deleted) {
      u8g2.setCursor(0, 67);
      u8g2.print("Log deleted");
    } else {
      logbook_card::drawFlightCard(summary);

      if (cursor_position == 0) {
        u8g2.drawRBox(0, 156 - 13, 96, 15, 2);
        u8g2.setDrawColor(0);
      }
      u8g2.setCursor(2, 156);
      u8g2.print("Delete");
      u8g2.setCursor(cursor_position == 0 ? 65 : 80, 156);
      if (cursor_position == 0) {
        u8g2.print("HOLD");
      } else {
        u8g2.print((char)126);
      }
      u8g2.setDrawColor(1);
    }

    if (deletePending) {
      uint8_t width =
          deletePending >= DELETE_HOLD_COUNT ? 96 : deletePending * 96 / DELETE_HOLD_COUNT;
      u8g2.drawBox(0, 173, width, 4);
    }

    u8g2.setFont(leaf_6x12);
    if (cursor_position == CURSOR_BACK) {
      u8g2.drawRBox(MENU_INPUT_X - 10, MENU_BACK_Y - 14, 34, 16, 2);
    }
    u8g2.setCursor(2, MENU_BACK_Y);
    u8g2.print("Back");
    u8g2.setCursor(MENU_INPUT_X, MENU_BACK_Y);
    u8g2.setDrawColor(cursor_position == CURSOR_BACK ? 0 : 1);
    u8g2.print((char)124);
    u8g2.setDrawColor(1);
  } while (u8g2.nextPage());
}

void PageFlightSummary::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == CURSOR_BACK) {
    if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
      speaker.playSound(state == ButtonEvent::HELD ? fx::exit : fx::cancel);
      pop_page();
    }
    return;
  }

  if (cursor_position == 0 && !deleted) {
    if (state == ButtonEvent::INCREMENTED) {
      deletePending = count;
      if (count >= DELETE_HOLD_COUNT) {
        buttons.consumeButton();
        deleted = LogbookEntryFile::deleteFiles(logbookPath, trackPath);
        speaker.playSound(deleted ? fx::confirm : fx::bad);
        deletePending = 0;
      }
    } else if (state == ButtonEvent::RELEASED || state == ButtonEvent::CLICKED) {
      deletePending = 0;
    }
  }
}
