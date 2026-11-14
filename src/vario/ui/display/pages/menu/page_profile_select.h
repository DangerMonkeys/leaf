#pragma once

#include <Arduino.h>
#include <memory>

#include "profiles/profile_store.h"
#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class PageProfileSelect : public MenuPage {
 public:
  PageProfileSelect();

  void show();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;
  void draw() override;

 protected:
  void closed(bool removed) override;

 private:
  enum class RowType : uint8_t {
    Header,
    Pilot,
    Glider,
  };

  static constexpr uint8_t VISIBLE_ROWS = 10;

  void refresh();
  void moveCursorDown();
  void moveCursorUp();
  void ensureCursorVisible();
  void selectCurrent();
  void close();
  void drawContentRow(uint8_t y, uint8_t contentIndex);
  void drawProfileRow(uint8_t y, const char* text, bool selected, bool checked);
  void drawBackRow();
  void drawStatus();
  void drawEmptyProfilesMessage();
  void drawCenteredText(uint8_t y, const char* text);
  void drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth);
  bool cursorOnBack() const;
  bool selectableRow(uint8_t contentIndex) const;
  RowType rowType(uint8_t contentIndex) const;
  uint8_t rowProfileIndex(uint8_t contentIndex) const;

  struct Model {
    ProfilePickerList pilots;
    ProfilePickerList gliders;
  };
  std::unique_ptr<Model> model_;
  char status_[32];
  uint8_t pilotCount_ = 0;
  uint8_t gliderCount_ = 0;
  uint8_t contentCount_ = 0;
  uint8_t firstVisible_ = 0;
};
