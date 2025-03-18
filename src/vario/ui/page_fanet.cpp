#include "page_fanet.h"
#include "page_fanet_ground_select.h"
#include "page_fanet_neighbors.h"
#include "page_fanet_stats.h"
#include "page_list_select.h"
#include "settings.h"

// Singleton instance

void PageFanet::setting_change(Button dir, ButtonState state, uint8_t count) {
  if (state != RELEASED) return;

  // Handle menu item selection
  switch (cursor_position) {
    case 0:
      // Ground Tracking Selection
      PageFanetGroundSelect::show();
      break;
    case 1: {
      // User selected Region select

      // Callback for when the region is changed
      auto regionChanged = [](int selected) {
        settings.fanet_region = (FanetRadioRegion)selected;
        Serial.print("Updating Fanet region to ");
        Serial.print(settings.fanet_region);
        Serial.print(" ");
        Serial.println(settings.fanet_region.c_str());
        settings.save();
      };

      // Bring up a dialogue for the user to change the region
      PageListSelect::show("Region", etl::array_view<const char*>(FanetRadioRegion::strings),
                           (int)settings.fanet_region, regionChanged);
    } break;
    case 2:
      // User selected statistics
      PageFanetStats::show();
      break;
    case 3:
      // User wants to see neighbors
      PageFanetNeighbors::show();
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
      ret = settings.fanet_region.c_str();
      break;
  }

  u8g2.print(ret);
}

PageFanet& PageFanet::getInstance() {
  static PageFanet instance;
  return instance;
}
