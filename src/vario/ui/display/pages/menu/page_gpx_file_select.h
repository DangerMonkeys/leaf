#pragma once

#include <Arduino.h>
#include <memory>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class PageGpxFileSelect : public MenuPage {
 public:
  PageGpxFileSelect();

  void show();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;
  void draw() override;

 protected:
  void closed(bool removed) override;

 private:
  enum class Mode : uint8_t {
    FolderSelect,
    NavFiles,
    SavedRoutes,
  };

  static constexpr uint8_t MAX_GPX_FILES = 32;
  static constexpr uint8_t MAX_GPX_FILENAME_LENGTH = 96;
  static constexpr uint8_t VISIBLE_FILE_ROWS = 10;

  void refreshIndex();
  void refreshFolders();
  void openSelectedFolder();
  bool ensureDirectory();
  void addFileName(const String& name);
  void moveCursorDown();
  void moveCursorUp();
  void ensureCursorVisible();
  void close();
  void closeToFolderSelect();
  void loadSelectedFile();
  void drawFileRow(uint8_t y, uint8_t fileIndex);
  void drawBackRow();
  void drawStatus();
  void drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth);
  bool cursorOnBack() const;
  const char* directoryPath() const;
  bool supportedFileName(const char* name) const;

  using FileName = char[MAX_GPX_FILENAME_LENGTH + 1];
  std::unique_ptr<FileName[]> fileNames_;
  char status_[32];
  bool loadFailed_ = false;
  Mode mode_ = Mode::NavFiles;
  uint8_t fileCount_ = 0;
  uint8_t firstVisible_ = 0;
  bool tooManyFiles_ = false;
};
