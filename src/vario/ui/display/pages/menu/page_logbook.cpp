#include "ui/display/pages/menu/page_logbook.h"

#include "hardware/buttons.h"
#include "logbook/logbook_io.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/logbook_card.h"
#include "ui/display/message_box.h"
#include "ui/display/pages.h"
#include "ui/settings/settings.h"

namespace {
  constexpr uint8_t MENU_INPUT_X = 74;
  constexpr uint8_t MENU_BACK_Y = 190;
  constexpr uint8_t DELETE_HOLD_COUNT = 5;
  constexpr uint8_t LEAF_LOG_ICON_UPLOADED = 140;
  constexpr uint8_t LEAF_LOG_ICON_NOT_UPLOADED = 141;
  constexpr uint8_t LEAF_LOG_ICON_REJECTED = 143;
}  // namespace

PageLogbook::PageLogbook() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = cursor_page;
}

void PageLogbook::showNewest() {
  modal = false;
  cursor_position = CURSOR_BACK;
  deletePending = 0;
  loadNewest();
}

void PageLogbook::showModalNewest() {
  modal = true;
  cursor_position = CURSOR_BACK;
  deletePending = 0;
  loadNewest();
  push_page(this);
}

void PageLogbook::loadNewest() {
  currentPath = "";
  scanDirection = 0;
  scan.begin();
  waitingForScan = true;
  cursor_max = CURSOR_BACK;
}

void PageLogbook::loadPath(const String& path) {
  currentPath = LogbookStore::normalizePath(path);
  deletePending = 0;
  scanDirection = 0;
  scan.begin(currentPath);
  waitingForScan = true;
}

void PageLogbook::loadAdjacent(bool newer) {
  if (currentPath.isEmpty() || waitingForScan) return;
  scanDirection = newer ? 1 : -1;
  scan.begin(currentPath);
  waitingForScan = true;
}

void PageLogbook::deleteCurrent() {
  if (currentPath.isEmpty()) return;

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
    // Refresh what remains without a per-file alert or recovery that could undo
    // a partial deletion. A still-present flight remains available to delete again.
    if (LogbookStore::readSummary(currentPath, summary))
      LogbookStore::entryPositionNewestFirst(currentPath, position, total);
    else
      loadNewest();
  }
}

bool PageLogbook::cursorUsesLeftButton() const { return cursor_position == cursor_page; }

void PageLogbook::draw() {
  // Also advance in direct simulator/gallery draws; the normal loop services this between frames.
  scan.service();
  if (waitingForScan && !scan.pending()) {
    waitingForScan = false;
    unavailable = scan.unavailable();
    const auto& nav = scan.navigation();
    total = nav.total;
    if (scanDirection) {
      const String adjacent = scanDirection > 0 ? nav.nextPath : nav.previousPath;
      if (!adjacent.isEmpty()) {
        currentPath = adjacent;
        position = scanDirection > 0 ? nav.position - 1 : nav.position + 1;
        speaker.playSound(scanDirection > 0 ? fx::increase : fx::decrease);
      } else {
        position = nav.position;
        speaker.playSound(fx::cancel);
      }
    } else {
      if (currentPath.isEmpty()) currentPath = scan.newestPath();
      position = nav.position;
    }
    summary = LogbookEntrySummary();
    if (!unavailable && !currentPath.isEmpty()) LogbookStore::readSummary(currentPath, summary);
    cursor_max = total ? cursor_page : CURSOR_BACK;
  }
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Logbook", menu_ui::GLYPH_LOGGING);
    if (waitingForScan) {
      message_box::draw("Opening", "logbook");
    } else if (summary.valid) {
      if (settings.labs_leafLog && summary.leafLogStatus != LeafLogFlightStatus::NotApplicable) {
        u8g2.setFont(leaf_8x14);
        u8g2.setCursor(menu_ui::ICON_CHECKBOX_X, 14);
        if (summary.leafLogStatus == LeafLogFlightStatus::Uploaded) {
          menu_ui::printGlyph(LEAF_LOG_ICON_UPLOADED);
        } else if (summary.leafLogStatus == LeafLogFlightStatus::NotUploaded) {
          menu_ui::printGlyph(LEAF_LOG_ICON_NOT_UPLOADED);
        } else {
          menu_ui::printGlyph(LEAF_LOG_ICON_REJECTED);
        }
      }
      drawEntry();
    } else {
      drawEmpty();
    }
    drawBackRow();
  } while (u8g2.nextPage());
}

void PageLogbook::drawEmpty() {
  if (unavailable || !sdcard.firmwareCanAccessFilesystem())
    message_box::draw("Unavailable");
  else if (total)
    message_box::draw("Log unavailable", "Run Repair Logs");
  else
    message_box::draw("No flights");
  if (total) {
    drawDeleteRow(156);
    drawPageRow(171);
  }
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
    menu_ui::beginRow(MENU_BACK_Y, true);
  } else {
    menu_ui::beginRow(MENU_BACK_Y, false);
  }

  menu_ui::drawLabel(2, MENU_BACK_Y, "Back");
  menu_ui::drawBackIcon(MENU_INPUT_X, MENU_BACK_Y);
  menu_ui::endRow();
}

void PageLogbook::drawDeleteRow(uint8_t y) {
  if (cursor_position == cursor_delete) {
    menu_ui::beginRow(y, true);
  } else {
    menu_ui::beginRow(y, false);
  }

  u8g2.setCursor(2, y);
  u8g2.print("Delete");
  u8g2.setCursor(cursor_position == cursor_delete ? menu_ui::HOLD_X : menu_ui::ICON_X, y);
  if (cursor_position == cursor_delete) {
    u8g2.print("HOLD");
  } else {
    menu_ui::printGlyph(menu_ui::ICON_ENTER);
  }
  menu_ui::endRow();
}

void PageLogbook::drawPageRow(uint8_t y) {
  if (cursor_position == cursor_page) {
    menu_ui::beginRow(y, true);
  } else {
    menu_ui::beginRow(y, false);
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

  menu_ui::endRow();
}

void PageLogbook::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == cursor_back) {
    if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
      speaker.playSound(state == ButtonEvent::HELD ? fx::exit : fx::cancel);
      scan.cancel();
      if (modal) {
        modal = false;
        mainMenuPage.backToMainMenu();
        pop_page();
      } else {
        logMenuPage.backToLogMenu();
      }
      if (state == ButtonEvent::HELD) {
        mainMenuPage.quitMenu();
      }
    }
    return;
  }

  if (waitingForScan) return;

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
    if (state == ButtonEvent::INCREMENTED && (dir == Button::CENTER || dir == Button::RIGHT)) {
      deletePending = count;
      if (count >= DELETE_HOLD_COUNT) {
        deleteCurrent();
      }
    } else if (state == ButtonEvent::RELEASED || state == ButtonEvent::CLICKED) {
      deletePending = 0;
    }
  }
}
