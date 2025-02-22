#pragma once

#include "display.h"
#include "fonts.h"
#include "menu_page.h"

/// @brief Page for display a message to the user, with a "close" button
class PageMessage : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return title.c_str(); }
  static void show(const char* title, const char* message) {
    instance().title = title;
    instance().message = message;
    push_page(&instance());
  }

  void draw_extra() override {
    u8g2.setFont(leaf_6x12);
    auto y = 40;
    const auto OFFSET = 14;  // Font is 10px high, allow for margin
    u8g2.setCursor(2, y);

    int end = message.length() - 1;
    int start = 0;
    while ((end = message.indexOf('\n', start)) != -1) {
      String line = message.substring(start, end);
      u8g2.setCursor(0, y);
      u8g2.print(line);
      y += OFFSET;
      start = end + 1;
    }
  }

 private:
  PageMessage() {}
  String title;
  String message;

  /// @brief Gets instance of singleton class
  /// @return
  static PageMessage& instance();
};