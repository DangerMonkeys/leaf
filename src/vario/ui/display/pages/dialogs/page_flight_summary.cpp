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

      menu_ui::beginRow(156, cursor_position == 0);
      menu_ui::drawLabel(2, 156, "Delete");
      if (cursor_position == 0) {
        u8g2.setCursor(65, 156);
        u8g2.print("HOLD");
      }
      menu_ui::endRow();
    }

    if (deletePending) {
      uint8_t width =
          deletePending >= DELETE_HOLD_COUNT ? 96 : deletePending * 96 / DELETE_HOLD_COUNT;
      u8g2.drawBox(0, 173, width, 4);
    }

    u8g2.setFont(leaf_6x12);
    menu_ui::beginRow(MENU_BACK_Y, cursor_position == CURSOR_BACK);
    menu_ui::drawLabel(2, MENU_BACK_Y, "Back");
    menu_ui::drawBackIcon(74, MENU_BACK_Y);
    menu_ui::endRow();
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
