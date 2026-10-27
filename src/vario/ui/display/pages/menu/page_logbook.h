#pragma once

#include <Arduino.h>

#include "logbook/logbook_store.h"
#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class PageLogbook : public SettingsMenuPage {
 public:
  PageLogbook();

  void showNewest();
  void showModalNewest();
  void draw() override;
  void service() { scan.service(); }
  bool scanning() const { return scan.pending(); }

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count) override;
  bool cursorUsesLeftButton() const override;
  void closed(bool removed) override {
    if (removed) scan.cancel();
  }

 private:
  enum Cursor : int8_t {
    cursor_back = CURSOR_BACK,
    cursor_delete = 0,
    cursor_page = 1,
  };

  void loadNewest();
  void loadPath(const String& path);
  void loadAdjacent(bool newer);
  void deleteCurrent();
  void drawEmpty();
  void drawEntry();
  void drawBackRow();
  void drawDeleteRow(uint8_t y);
  void drawPageRow(uint8_t y);

  String currentPath;
  LogbookEntrySummary summary;
  uint16_t position = 0;
  uint16_t total = 0;
  uint8_t deletePending = 0;
  bool modal = false;
  LogbookScan scan;
  int8_t scanDirection = 0;
  bool waitingForScan = false;
  bool unavailable = false;
};
