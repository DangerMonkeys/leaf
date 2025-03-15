#pragma once

#include "etl/array.h"
#include "etl/array_view.h"
#include "menu_page.h"

/// @brief Page for selecting the Fanet Ground Tracking mode to enable
class PageFanetGroundSelect : SimpleSettingsMenuPage {
 public:
  static void show();
  const char* get_title() const override { return "Currently..."; }

  etl::array_view<const char*> get_labels() const override {
    static etl::array labels{
        "Walking", "Vehicle", "Need Ride", "Landed OK", "Tech Sup",
    };
    return etl::array_view<const char*>(labels);
  }

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count) override;

 private:
  PageFanetGroundSelect() {}
  static PageFanetGroundSelect& getInstance();
};