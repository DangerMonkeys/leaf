#pragma once

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

enum class NavDataSelectMode : uint8_t {
  Point,
  Route,
};

class PageNavDataSelect : public MenuPage {
 public:
  PageNavDataSelect();

  void show(NavDataSelectMode mode);
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;
  void draw() override;

 private:
  static constexpr uint8_t VISIBLE_ROWS = 10;
  static constexpr uint8_t TEXT_MAX_WIDTH = 92;

  uint8_t itemCount() const;
  const char* itemName(uint8_t index) const;
  void moveCursorDown();
  void moveCursorUp();
  void ensureCursorVisible();
  void close();
  void selectCurrent();
  void drawItemRow(uint8_t y, uint8_t index);
  void drawBackRow();
  void drawStatus();
  void drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth);
  bool cursorOnBack() const;

  NavDataSelectMode mode_ = NavDataSelectMode::Point;
  uint8_t firstVisible_ = 0;
};
