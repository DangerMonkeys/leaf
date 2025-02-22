#include "page_fanet.h"
#include "page_fanet_ground_select.h"
#include "page_list_select.h"

// Singleton instance
static etl::array regions{"OFF", "US", "EU"};

void PageFanet::setting_change(Button dir, ButtonState state, uint8_t count) {
  if (state != RELEASED) return;

  // Handle menu item selection
  switch (cursor_position) {
    case 0:
      // Ground Tracking Selection
      PageFanetGroundSelect::show();
      break;
    case 1:
      // User selected Region select

      // Callback for when the region is changed
      auto regionChanged = [](int selected) {
        // TODO: Save the selected region
        Serial.println("Selected region: " + String(selected));
      };

      // Bring up a dialogue for the user to change the region
      PageListSelect::show("Region", etl::array_view<const char*>(regions), 0, regionChanged);
      break;
  }

  // Call parent class to handle back button
  SimpleSettingsMenuPage::setting_change(dir, state, count);
}

void PageFanet::draw_menu_input(int8_t cursor_position) {
  String ret((char)126);

  switch (cursor_position) {
    case 1:
      // Region
      // This will eventually come from settings (US, EU, etc)
      ret = "OFF";
      break;
  }

  u8g2.print(ret);
}

PageFanet& PageFanet::getInstance() {
  static PageFanet instance;
  return instance;
}
