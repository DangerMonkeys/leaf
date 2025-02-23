#pragma once

#include "menu_page.h"

enum class FanetGroundTrackingMode : uint8_t { WALKING, VEHICLE, NEED_RIDE, LANDED_OK, TECH_SUP };

class PageFanetGround : public SimpleSettingsMenuPage {
 public:
  static void show(FanetGroundTrackingMode mode);
  const char* get_title() const override;
  virtual void closed(bool removed_from_Stack) override;
  void draw_extra() override;

 private:
  PageFanetGround() {}
  FanetGroundTrackingMode mode;
  static PageFanetGround& getInstance();
};