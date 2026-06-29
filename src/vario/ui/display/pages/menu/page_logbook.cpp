#include "ui/display/pages/menu/page_logbook.h"

#include "hardware/buttons.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/logbook_card.h"
#include "ui/display/pages.h"

namespace {
  constexpr uint8_t MENU_INPUT_X = 74;
  constexpr uint8_t MENU_BACK_Y = 190;
  constexpr uint8_t DELETE_HOLD_COUNT = 5;
}  // namespace

PageLogbook::PageLogbook() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = cursor_page;
}

void PageLogbook::showNewest() {
  cursor_position = CURSOR_BACK;
  deletePending = 0;
  loadNewest();
}

void PageLogbook::loadNewest() {
  String path;
  if (!LogbookStore::newestEntryPath(path)) {
    currentPath = "";
    summary = LogbookEntrySummary();
    position = 0;
    total = 0;
    cursor_max = CURSOR_BACK;
    cursor_position = CURSOR_BACK;
    return;
  }
  loadPath(path);
}

void PageLogbook::loadPath(const String& path) {
  currentPath = LogbookStore::normalizePath(path);
  deletePending = 0;
  if (!LogbookStore::readSummary(currentPath, summary)) {
    summary = LogbookEntrySummary();
    position = 0;
    total = LogbookStore::count();
    cursor_max = CURSOR_BACK;
    cursor_position = CURSOR_BACK;
    return;
  }
  cursor_max = cursor_page;
  LogbookStore::entryPositionNewestFirst(currentPath, position, total);
}

void PageLogbook::loadAdjacent(bool newer) {
  if (!summary.valid) return;

  String path;
  const bool found = newer ? LogbookStore::nextEntryPath(currentPath, path)
                           : LogbookStore::previousEntryPath(currentPath, path);
  if (found) {
    speaker.playSound(newer ? fx::increase : fx::decrease);
    loadPath(path);
  } else {
    speaker.playSound(fx::cancel);
  }
}

void PageLogbook::deleteCurrent() {
  if (!summary.valid) return;

  String replacementPath;
  if (!LogbookStore::previousEntryPath(currentPath, replacementPath)) {
    LogbookStore::nextEntryPath(currentPath, replacementPath);
  }

  if (LogbookStore::deleteEntry(currentPath)) {
    buttons.consumeButton();
    speaker.playSound(fx::confirm);
    if (replacementPath.isEmpty()) {
      loadNewest();
    } else {
      loadPath(replacementPath);
    }
  } else {
    deletePending = 0;
    speaker.playSound(fx::bad);
  }
}

void PageLogbook::draw() {
  u8g2.firstPage();
  do {
    display_menuTitle("LOGBOOK");
    if (summary.valid) {
      drawEntry();
    } else {
      drawEmpty();
    }
    drawBackRow();
  } while (u8g2.nextPage());
}

void PageLogbook::drawEmpty() {
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 67);
  u8g2.print("No flights");
}

void PageLogbook::drawEntry() {
  logbook_card::drawFlightCard(summary);

  drawDeleteRow(156);
  drawPageRow(171);

  if (deletePending) {
    uint8_t width =
        deletePending >= DELETE_HOLD_COUNT ? 96 : deletePending * 96 / DELETE_HOLD_COUNT;
    u8g2.drawBox(0, 173, width, 4);
  }
}

void PageLogbook::drawBackRow() {
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
}

void PageLogbook::drawDeleteRow(uint8_t y) {
  if (cursor_position == cursor_delete) {
    u8g2.drawRBox(0, y - 13, 96, 15, 2);
    u8g2.setDrawColor(0);
  }

  u8g2.setCursor(2, y);
  u8g2.print("Delete");
  u8g2.setCursor(cursor_position == cursor_delete ? 65 : 80, y);
  if (cursor_position == cursor_delete) {
    u8g2.print("HOLD");
  } else {
    u8g2.print((char)126);
  }
  u8g2.setDrawColor(1);
}

void PageLogbook::drawPageRow(uint8_t y) {
  if (cursor_position == cursor_page) {
    u8g2.drawRBox(0, y - 13, 96, 15, 2);
    u8g2.setDrawColor(0);
  }

  if (position > 1) {
    u8g2.setCursor(2, y);
    u8g2.print((char)140);
  }

  String pageText = String(position) + "/" + String(total);
  u8g2.setCursor(48 - (pageText.length() * 3), y);
  u8g2.print(pageText);

  if (position < total) {
    u8g2.setCursor(80, y);
    u8g2.print((char)126);
  }

  u8g2.setDrawColor(1);
}

void PageLogbook::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == cursor_back) {
    if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
      speaker.playSound(state == ButtonEvent::HELD ? fx::exit : fx::cancel);
      logMenuPage.backToLogMenu();
      if (state == ButtonEvent::HELD) {
        mainMenuPage.backToMainMenu();
      }
    }
    return;
  }

  if (cursor_position == cursor_page) {
    if (state == ButtonEvent::CLICKED) {
      if (dir == Button::LEFT) {
        loadAdjacent(true);
      } else if (dir == Button::RIGHT) {
        loadAdjacent(false);
      }
    }
    return;
  }

  if (cursor_position == cursor_delete) {
    if (state == ButtonEvent::INCREMENTED) {
      deletePending = count;
      if (count >= DELETE_HOLD_COUNT) {
        deleteCurrent();
      }
    } else if (state == ButtonEvent::RELEASED || state == ButtonEvent::CLICKED) {
      deletePending = 0;
    }
  }
}
