#include "page_fanet_ground.h"

void PageFanetGround::show(FanetGroundTrackingMode mode) {
  getInstance().mode = mode;
  push_page(&getInstance());
}

const char* PageFanetGround::get_title() const {
  switch (mode) {
    case FanetGroundTrackingMode::WALKING:
      return "Waking";
    case FanetGroundTrackingMode::VEHICLE:
      return "Vehicle";
    case FanetGroundTrackingMode::NEED_RIDE:
      return "Need Ride";
    case FanetGroundTrackingMode::LANDED_OK:
      return "Landed OK";
    case FanetGroundTrackingMode::TECH_SUP:
      return "Tech Support";
    default:
      return "Unknown";
  }
}

PageFanetGround& PageFanetGround::getInstance() {
  static PageFanetGround instance;
  return instance;
}
