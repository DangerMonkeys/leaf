#ifndef PageMenuLog_h
#define PageMenuLog_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/display/pages/menu/page_logbook.h"
#include "ui/display/pages/menu/page_repair_logs.h"
#include "ui/input/buttons.h"

class LogMenuPage : public SettingsMenuPage {
 public:
  LogMenuPage() {
    cursor_position = 0;
    cursor_max = 4;
  }
  void draw();
  void backToLogMenu();
  void showLogbook();
  void serviceLogbook() { pageLogbook.service(); }
  bool logbookScanning() const { return pageLogbook.scanning(); }
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  void drawLogMenu();
  static constexpr const char* labels[5] = {"Back", "Save IGC", "AutoStart", "AutoStop",
                                            "Repair Logs"};
  PageLogbook pageLogbook;
  PageRepairLogs repairPage;
};

extern LogMenuPage logMenuPage;

#endif
