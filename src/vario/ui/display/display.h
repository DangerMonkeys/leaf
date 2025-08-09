#pragma once

#include <U8g2lib.h>
#include "hardware/configuration.h"

// Display settings loaded from configuration / variants
#ifndef WO256X128                               // if not old hardare, use the latest:
extern U8G2_ST75256_JLX19296_F_4W_HW_SPI u8g2;  // Leaf 3.2.3+  Alice Green HW
#else  // otherwise use the old hardware settings from v3.2.2:
extern U8G2_ST75256_WO256X128_F_4W_HW_SPI u8g2;  // Leaf 3.2.2 June Hung
#endif

#define LCD_BACKLIGHT 21  // can be used for backlight if desired (also broken out to header)
#define LCD_RS 17         // 16 on old V3.2.0
#define LCD_RESET 18      // 17 on old V3.2.0

void GLCD_inst(byte data);
void GLCD_data(byte data);
// void GLCD_init(void);

// keep track of pages
enum display_page_actions {
  page_home,  // go to home screen (probably thermal page)
  page_prev,  // go to page -1
  page_next,  // go to page +1
  page_back  // go to page we were just on before (i.e. step back in a menu tree, or cancel a dialog
             // page back to previous page)
};

enum display_main_pages {
  page_debug,
  page_thermal,
  page_thermalAdv,
  page_nav,
  page_menu,
  page_last,
  page_charging
};

class Display {
 public:
  void init();
  void update();
  void clear();
  void setContrast(uint8_t contrast);

  void turnPage(uint8_t action);
  void setPage(uint8_t targetPage);
  uint8_t getPage();

  void showPageDebug();
  void showPageCharging();
  void showOnSplash();

  bool displayingWarning();
  void dismissWarning();
};

extern Display display;
