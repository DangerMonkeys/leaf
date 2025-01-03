#pragma once

#include "menu_page.h"

// Provides a simple dialog to display a QR code
class PageQR : public SimpleSettingsMenuPage {
 public:
  PageQR(String title, String url) : title(title), url(url) {}
  const char* get_title() const override { return title.c_str(); }
  void draw_extra() override;

 private:
  String title;
  String url;
};