#pragma once

#include <U8g2lib.h>

#include "hardware/configuration.h"
#include "utils/constrained_enum.h"

// Display settings loaded from configuration / variants
#ifndef WO256X128                               // if not old hardare, use the latest:
extern U8G2_ST75256_JLX19296_F_4W_HW_SPI u8g2;  // Leaf 3.2.3+  Alice Green HW
#else  // otherwise use the old hardware settings from v3.2.2:
extern U8G2_ST75256_WO256X128_F_4W_HW_SPI u8g2;  // Leaf 3.2.2 June Hung
#endif

enum class PageAction : uint8_t {
  Home,  // go to home screen (probably thermal page)
  Prev,  // go to page -1
  Next,  // go to page +1
  Back   // go to page we were just on before (i.e. step back in a menu tree, or cancel a dialog
         // page back to previous page)
};

enum class MainPage : uint8_t {
  Debug = 0,
  Thermal = 1,
  ThermalAdv = 2,
  Nav = 3,
  Menu = 4,
  Charging = 5
};
DEFINE_WRAPPING_BOUNDS(MainPage, MainPage::Debug, MainPage::Menu);

class Display {
 public:
  void init();
  void update();
  void clear();
  void setContrast(uint8_t contrast);

  void turnPage(PageAction action);
  void setPage(MainPage targetPage);
  MainPage getPage() { return displayPage_; }

  void showPageDebug();
  void showPageCharging();
  void showOnSplash();

  bool displayingWarning() { return showWarning_; }
  void dismissWarning() { showWarning_ = false; }

 private:
  MainPage displayPage_ = MainPage::Thermal;

  // track the page we used to be on, so we can "go back" if needed (like cancelling out of a menu
  // heirarchy)
  MainPage displayPagePrior_ = MainPage::Thermal;

  uint8_t showSplashScreenFrames_ = 0;

  bool showWarning_ = true;
};

extern Display display;
