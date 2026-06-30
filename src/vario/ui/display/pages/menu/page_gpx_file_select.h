#pragma once

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class PageGpxFileSelect : public MenuPage {
 public:
  PageGpxFileSelect();

  void show();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;
  void draw() override;

 private:
  static constexpr uint8_t MAX_GPX_FILES = 32;
  static constexpr uint8_t MAX_GPX_FILENAME_LENGTH = 96;
  static constexpr uint8_t VISIBLE_FILE_ROWS = 10;

  void refreshIndex();
  bool ensureGpxDirectory();
  void addFileName(const String& name);
  void moveCursorDown();
  void moveCursorUp();
  void ensureCursorVisible();
  void close();
  void loadSelectedFile();
  void drawFileRow(uint8_t y, uint8_t fileIndex);
  void drawBackRow();
  void drawStatus();
  void drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth);
  bool cursorOnBack() const;

  char fileNames_[MAX_GPX_FILES][MAX_GPX_FILENAME_LENGTH + 1];
  char status_[32];
  uint8_t fileCount_ = 0;
  uint8_t firstVisible_ = 0;
  bool tooManyFiles_ = false;
};
