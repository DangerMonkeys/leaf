#include "ui/display/pages/menu/page_nav_data_select.h"

#include <string.h>

#include "navigation/gpx.h"
#include "navigation/nav_ids.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"

namespace {
  constexpr uint8_t ROW_START_Y = 35;
  constexpr uint8_t ROW_SPACING = 15;
  constexpr uint8_t MENU_INPUT_X = 74;
  constexpr uint8_t MENU_BACK_Y = 190;
}  // namespace

PageNavDataSelect::PageNavDataSelect() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = CURSOR_BACK;
}

void PageNavDataSelect::show(NavDataSelectMode mode) {
  mode_ = mode;
  firstVisible_ = 0;

  const uint8_t count = itemCount();
  if (count == 0) {
    cursor_position = CURSOR_BACK;
    cursor_max = CURSOR_BACK;
  } else {
    cursor_position = 0;
    cursor_max = count - 1;
  }

  push_page(this);
}

bool PageNavDataSelect::button_event(Button button, ButtonEvent state, uint8_t count) {
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
  } else if (state == ButtonEvent::INCREMENTED) {
    switch (button) {
      case Button::UP:
        pageCursorUp();
        break;
      case Button::DOWN:
        pageCursorDown();
        break;
      default:
        break;
    }
  }

  return true;
}

void PageNavDataSelect::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Navigate\037To", menu_ui::GLYPH_NAV_DATA);

    const uint8_t count = itemCount();
    if (count == 0) {
      drawStatus();
    } else {
      ensureCursorVisible();
      const uint8_t remainingRows = count - firstVisible_;
      const uint8_t visibleCount = remainingRows < VISIBLE_ROWS ? remainingRows : VISIBLE_ROWS;
      for (uint8_t i = 0; i < visibleCount; ++i) {
        const uint8_t itemIndex = firstVisible_ + i;
        drawItemRow(ROW_START_Y + i * ROW_SPACING, itemIndex);
      }
    }

    drawBackRow();
  } while (u8g2.nextPage());
}

uint8_t PageNavDataSelect::itemCount() const {
  return navigator.totalRoutes + navigator.totalWaypoints;
}

const char* PageNavDataSelect::itemName(uint8_t index) const {
  if (itemIsRoute(index)) {
    return navigator.routes[index + 1].name;
  }
  const uint8_t waypointIndex = index - navigator.totalRoutes + 1;
  return navigator.waypoint(WaypointID(waypointIndex)).name;
}

bool PageNavDataSelect::itemIsRoute(uint8_t index) const { return index < navigator.totalRoutes; }

void PageNavDataSelect::moveCursorDown() {
  const uint8_t count = itemCount();
  if (count == 0) {
    cursor_position = CURSOR_BACK;
    return;
  }

  if (cursorOnBack()) {
    cursor_position = 0;
  } else if (cursor_position >= static_cast<int8_t>(count - 1)) {
    cursor_position = CURSOR_BACK;
  } else {
    ++cursor_position;
  }

  ensureCursorVisible();
  playCursorMoveSound();
}

void PageNavDataSelect::moveCursorUp() {
  const uint8_t count = itemCount();
  if (count == 0) {
    cursor_position = CURSOR_BACK;
    return;
  }

  if (cursorOnBack()) {
    cursor_position = count - 1;
  } else if (cursor_position <= 0) {
    cursor_position = CURSOR_BACK;
  } else {
    --cursor_position;
  }

  ensureCursorVisible();
  playCursorMoveSound();
}

void PageNavDataSelect::pageCursorDown() {
  const uint8_t count = itemCount();
  if (count == 0 || cursorOnBack()) return;

  const int8_t lastItem = count - 1;
  const int8_t previous = cursor_position;
  const int16_t next = cursor_position + VISIBLE_ROWS;
  cursor_position = next > lastItem ? lastItem : next;

  if (cursor_position == previous) return;
  ensureCursorVisible();
  playCursorMoveSound();
}

void PageNavDataSelect::pageCursorUp() {
  const uint8_t count = itemCount();
  if (count == 0 || cursorOnBack()) return;

  const int8_t previous = cursor_position;
  const int16_t next = cursor_position - VISIBLE_ROWS;
  cursor_position = next < 0 ? 0 : next;

  if (cursor_position == previous) return;
  ensureCursorVisible();
  playCursorMoveSound();
}

void PageNavDataSelect::ensureCursorVisible() {
  if (cursorOnBack() || itemCount() == 0) return;

  if (cursor_position < firstVisible_) {
    firstVisible_ = cursor_position;
  } else if (cursor_position >= firstVisible_ + VISIBLE_ROWS) {
    firstVisible_ = cursor_position - VISIBLE_ROWS + 1;
  }
}

void PageNavDataSelect::close() {
  speaker.playSound(fx::cancel);
  navDataMenuPage.backToNavDataMenu();
  pop_page();
}

void PageNavDataSelect::selectCurrent() {
  const uint8_t count = itemCount();
  if (cursorOnBack() || cursor_position < 0 || cursor_position >= count) return;

  const bool activated =
      itemIsRoute(cursor_position)
          ? navigator.activateRoute(RouteID(cursor_position + 1))
          : navigator.activatePoint(WaypointID(cursor_position - navigator.totalRoutes + 1));
  if (activated) {
    navDataMenuPage.backToNavDataMenu();
    pop_page();
  } else {
    speaker.playSound(fx::bad);
  }
}

void PageNavDataSelect::drawItemRow(uint8_t y, uint8_t index) {
  const bool selected = cursor_position == index;
  menu_ui::beginRow(y, selected);
  u8g2.setCursor(2, y);
  menu_ui::printGlyph(itemIsRoute(index) ? menu_ui::GLYPH_ROUTE : menu_ui::GLYPH_WAYPOINT);
  drawFittedText(12, y, itemName(index), TEXT_MAX_WIDTH - 10);
  menu_ui::endRow();
}

void PageNavDataSelect::drawBackRow() {
  menu_ui::beginRow(MENU_BACK_Y, cursorOnBack());
  menu_ui::drawLabel(2, MENU_BACK_Y, "Back");
  menu_ui::drawBackIcon(MENU_INPUT_X, MENU_BACK_Y);
  menu_ui::endRow();
}

void PageNavDataSelect::drawStatus() {
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(2, 67);
  u8g2.print("No destinations");
}

void PageNavDataSelect::drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth) {
  char buffer[maxGpxNameLength + 1];
  strncpy(buffer, text, sizeof(buffer));
  buffer[maxGpxNameLength] = '\0';

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

bool PageNavDataSelect::cursorOnBack() const { return cursor_position == CURSOR_BACK; }
