#include "ui/display/pages/menu/page_profile_select.h"

#include <string.h>

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"

namespace {
  constexpr uint8_t ROW_START_Y = 31;
  constexpr uint8_t ROW_SPACING = 15;
  constexpr uint8_t MENU_BACK_Y = 190;
  constexpr uint8_t CHECKBOX_X = 83;
  constexpr uint8_t TEXT_MAX_WIDTH = 78;
  constexpr uint8_t MAX_PROFILE_NAME_LENGTH = 64;
  constexpr uint8_t DISPLAY_WIDTH = 96;

  String gliderName(const GliderProfile& glider) { return glider.resolvedDisplayName(); }
}  // namespace

PageProfileSelect::PageProfileSelect() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = CURSOR_BACK;
  status_[0] = '\0';
}

void PageProfileSelect::show() {
  refresh();
  push_page(this);
}

bool PageProfileSelect::button_event(Button button, ButtonEvent state, uint8_t count) {
  if (button == Button::NONE) return false;

  if (state == ButtonEvent::CLICKED) {
    switch (button) {
      case Button::UP:
        moveCursorUp();
        break;
      case Button::DOWN:
        moveCursorDown();
        break;
      case Button::LEFT:
        close();
        break;
      case Button::RIGHT:
      case Button::CENTER:
        if (cursorOnBack()) {
          close();
        } else {
          selectCurrent();
        }
        break;
      default:
        break;
    }
  }

  return true;
}

void PageProfileSelect::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Profiles", menu_ui::GLYPH_PROFILE);

    if (contentCount_ == 0) {
      drawStatus();
    } else {
      ensureCursorVisible();
      const uint8_t remainingRows = contentCount_ - firstVisible_;
      const uint8_t visibleCount = remainingRows < VISIBLE_ROWS ? remainingRows : VISIBLE_ROWS;
      for (uint8_t i = 0; i < visibleCount; ++i) {
        drawContentRow(ROW_START_Y + i * ROW_SPACING, firstVisible_ + i);
      }
    }

    u8g2.drawHLine(0, 174, 96);
    drawBackRow();
  } while (u8g2.nextPage());
}

void PageProfileSelect::refresh() {
  size_t pilotCount = 0;
  size_t gliderCount = 0;
  firstVisible_ = 0;
  status_[0] = '\0';

  if (!ProfileStore::load(pilots_, MAX_PILOT_PROFILES, pilotCount, gliders_, MAX_GLIDER_PROFILES,
                          gliderCount, activePilotId_, activeGliderId_)) {
    snprintf(status_, sizeof(status_), "Profiles not ready");
    pilotCount_ = 0;
    gliderCount_ = 0;
    contentCount_ = 0;
    cursor_position = CURSOR_BACK;
    cursor_max = CURSOR_BACK;
    return;
  }

  pilotCount_ = static_cast<uint8_t>(pilotCount);
  gliderCount_ = static_cast<uint8_t>(gliderCount);
  contentCount_ = 2 + pilotCount_ + gliderCount_;
  cursor_max = contentCount_ > 0 ? contentCount_ - 1 : CURSOR_BACK;

  if (pilotCount_ + gliderCount_ == 0) {
    contentCount_ = 0;
    cursor_position = CURSOR_BACK;
    cursor_max = CURSOR_BACK;
    return;
  }

  cursor_position = pilotCount_ > 0 ? 1 : 1 + pilotCount_ + 1;
}

void PageProfileSelect::moveCursorDown() {
  if (contentCount_ == 0) {
    cursor_position = CURSOR_BACK;
    return;
  }

  if (cursorOnBack()) {
    cursor_position = 0;
  } else if (cursor_position >= static_cast<int8_t>(contentCount_ - 1)) {
    cursor_position = CURSOR_BACK;
  } else {
    cursor_position++;
  }

  while (!cursorOnBack() && !selectableRow(cursor_position)) {
    if (cursor_position >= static_cast<int8_t>(contentCount_ - 1)) {
      cursor_position = CURSOR_BACK;
    } else {
      cursor_position++;
    }
  }

  ensureCursorVisible();
  playCursorMoveSound();
}

void PageProfileSelect::moveCursorUp() {
  if (contentCount_ == 0) {
    cursor_position = CURSOR_BACK;
    return;
  }

  if (cursorOnBack()) {
    cursor_position = contentCount_ - 1;
  } else if (cursor_position <= 0) {
    cursor_position = CURSOR_BACK;
  } else {
    cursor_position--;
  }

  while (!cursorOnBack() && !selectableRow(cursor_position)) {
    if (cursor_position <= 0) {
      cursor_position = CURSOR_BACK;
    } else {
      cursor_position--;
    }
  }

  ensureCursorVisible();
  playCursorMoveSound();
}

void PageProfileSelect::ensureCursorVisible() {
  if (cursorOnBack() || contentCount_ == 0) return;

  if (cursor_position < firstVisible_) {
    firstVisible_ = cursor_position;
  } else if (cursor_position >= firstVisible_ + VISIBLE_ROWS) {
    firstVisible_ = cursor_position - VISIBLE_ROWS + 1;
  }
}

void PageProfileSelect::selectCurrent() {
  if (cursorOnBack() || !selectableRow(cursor_position)) return;

  const RowType type = rowType(cursor_position);
  const uint8_t profileIndex = rowProfileIndex(cursor_position);
  bool selected = false;

  if (type == RowType::Pilot && profileIndex < pilotCount_) {
    selected = ProfileStore::selectPilot(pilots_[profileIndex].id);
  } else if (type == RowType::Glider && profileIndex < gliderCount_) {
    selected = ProfileStore::selectGlider(gliders_[profileIndex].id);
  }

  if (selected) {
    const int8_t previousCursor = cursor_position;
    speaker.playSound(fx::confirm);
    refresh();
    if (previousCursor >= 0 && previousCursor < static_cast<int8_t>(contentCount_) &&
        selectableRow(previousCursor)) {
      cursor_position = previousCursor;
      ensureCursorVisible();
    }
  } else {
    speaker.playSound(fx::bad);
    snprintf(status_, sizeof(status_), "Select failed");
  }
}

void PageProfileSelect::close() {
  speaker.playSound(fx::cancel);
  pop_page();
}

void PageProfileSelect::drawContentRow(uint8_t y, uint8_t contentIndex) {
  const RowType type = rowType(contentIndex);
  if (type == RowType::Header) {
    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, y - 2);
    u8g2.print(contentIndex == 0 ? "Pilot Profile:" : "Glider Profile:");
    u8g2.drawHLine(0, y - 1, 96);
    u8g2.setFont(leaf_6x12);
    return;
  }

  const uint8_t profileIndex = rowProfileIndex(contentIndex);
  if (type == RowType::Pilot && profileIndex < pilotCount_) {
    drawProfileRow(y, pilots_[profileIndex].name.c_str(), cursor_position == contentIndex,
                   isSelectedPilot(profileIndex));
  } else if (type == RowType::Glider && profileIndex < gliderCount_) {
    const String name = gliderName(gliders_[profileIndex]);
    drawProfileRow(y, name.c_str(), cursor_position == contentIndex,
                   isSelectedGlider(profileIndex));
  }
}

void PageProfileSelect::drawProfileRow(uint8_t y, const char* text, bool selected, bool checked) {
  menu_ui::beginRow(y, selected);
  drawFittedText(2, y, text, TEXT_MAX_WIDTH);
  u8g2.setCursor(CHECKBOX_X, y);
  menu_ui::printGlyph(checked ? menu_ui::ICON_ON : menu_ui::ICON_OFF);
  menu_ui::endRow();
}

void PageProfileSelect::drawBackRow() {
  u8g2.setFont(leaf_6x12);
  menu_ui::beginRow(MENU_BACK_Y, cursorOnBack());
  menu_ui::drawLabel(2, MENU_BACK_Y, "Back");
  menu_ui::drawBackIcon(74, MENU_BACK_Y);
  menu_ui::endRow();
}

void PageProfileSelect::drawStatus() {
  if (status_[0] != '\0') {
    u8g2.setFont(leaf_5x8);
    drawCenteredText(82, status_);
    return;
  }

  drawEmptyProfilesMessage();
}

void PageProfileSelect::drawEmptyProfilesMessage() {
  static constexpr const char* lines[] = {"No profiles",     "available.", "Use the Web App",
                                          "to create Pilot", "and Glider", "profiles."};
  static constexpr uint8_t firstLineY = 55;
  static constexpr uint8_t lineSpacing = 13;

  u8g2.setFont(leaf_5x8);
  for (uint8_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
    drawCenteredText(firstLineY + i * lineSpacing, lines[i]);
  }
}

void PageProfileSelect::drawCenteredText(uint8_t y, const char* text) {
  const int16_t width = u8g2.getStrWidth(text);
  const int16_t x = width < DISPLAY_WIDTH ? (DISPLAY_WIDTH - width) / 2 : 0;
  u8g2.setCursor(x, y);
  u8g2.print(text);
}

void PageProfileSelect::drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth) {
  char buffer[MAX_PROFILE_NAME_LENGTH + 1];
  strncpy(buffer, text == nullptr ? "" : text, sizeof(buffer));
  buffer[MAX_PROFILE_NAME_LENGTH] = '\0';

  size_t len = strlen(buffer);
  while (len > 2 && u8g2.getStrWidth(buffer) > maxWidth) {
    buffer[len - 3] = '.';
    buffer[len - 2] = '.';
    buffer[len - 1] = '\0';
    --len;
  }

  u8g2.setCursor(x, y);
  u8g2.print(buffer);
}

bool PageProfileSelect::cursorOnBack() const { return cursor_position == CURSOR_BACK; }

bool PageProfileSelect::selectableRow(uint8_t contentIndex) const {
  const RowType type = rowType(contentIndex);
  return type == RowType::Pilot || type == RowType::Glider;
}

bool PageProfileSelect::isSelectedPilot(uint8_t pilotIndex) const {
  if (pilotIndex >= pilotCount_) return false;
  if (!activePilotId_.isEmpty()) return pilots_[pilotIndex].id == activePilotId_;
  return pilotCount_ == 1;
}

bool PageProfileSelect::isSelectedGlider(uint8_t gliderIndex) const {
  if (gliderIndex >= gliderCount_) return false;
  if (!activeGliderId_.isEmpty()) return gliders_[gliderIndex].id == activeGliderId_;
  return gliderCount_ == 1;
}

PageProfileSelect::RowType PageProfileSelect::rowType(uint8_t contentIndex) const {
  if (contentIndex == 0 || contentIndex == pilotCount_ + 1) return RowType::Header;
  if (contentIndex <= pilotCount_) return RowType::Pilot;
  return RowType::Glider;
}

uint8_t PageProfileSelect::rowProfileIndex(uint8_t contentIndex) const {
  if (rowType(contentIndex) == RowType::Pilot) return contentIndex - 1;
  return contentIndex - pilotCount_ - 2;
}
