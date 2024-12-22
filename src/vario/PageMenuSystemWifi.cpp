#include "PageMenuSystemWifi.h"

void PageMenuSystemWifi::setting_change(Button dir, ButtonState state, uint8_t count) {
  if(state != RELEASED) return;

  // Handle updating items
  switch (cursor_position) {
    case cursor_system_wifi_setup:
      push_page(&page_wifi_setup);
      break;
    case cursor_system_wifi_update:
      push_page(&page_wifi_update);
      break;
  }

  // Call the parent class to handle the back button
  SimpleSettingsMenuPage::setting_change(dir, state, count);
}