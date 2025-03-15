#include "page_fanet_ground.h"
#include "Preferences.h"
#include "baro.h"
#include "display.h"
#include "displayFields.h"
#include "fanet_radio.h"
#include "fonts.h"
#include "gps.h"
#include "log.h"
#include "page_message.h"

void PageFanetGround::show(FanetGroundTrackingMode mode) {
  // If we're currently in a flight, show an error message
  if (flightTimer_isRunning()) {
    PageMessage::show("ERROR",
                      "End flight Before\n"
                      "Tracking Ground\n"
                      "Position");
    return;
  }

  if (FANET_region == FanetRadioRegion::OFF) {
    PageMessage::show("ERROR",
                      "Fanet Region\n"
                      "Not Set\n");
    return;
  }

  // Show this page
  getInstance().mode = mode;
  push_page(&getInstance());

  // Set our tracking mode to ground tracking.
  auto& radio = FanetRadio::getInstance();
  radio.begin(FANET_region);
  switch (mode) {
    case FanetGroundTrackingMode::WALKING:
      radio.setTrackingMode(Fanet::GroundTrackingType::Walking);
      break;
    case FanetGroundTrackingMode::VEHICLE:
      radio.setTrackingMode(Fanet::GroundTrackingType::Vehicle);
      break;
    case FanetGroundTrackingMode::NEED_RIDE:
      radio.setTrackingMode(Fanet::GroundTrackingType::Vehicle);
      break;
    case FanetGroundTrackingMode::LANDED_OK:
      radio.setTrackingMode(Fanet::GroundTrackingType::LandedWell);
      break;
    case FanetGroundTrackingMode::TECH_SUP:
      radio.setTrackingMode(Fanet::GroundTrackingType::NeedTechnicalSupport);
      break;
  };
}

const char* PageFanetGround::get_title() const { return "Ground Tracking"; }

void PageFanetGround::closed(bool removed_from_Stack) {
  // Ground tracking should only occur while we're showing this page.
  if (removed_from_Stack) FanetRadio::getInstance().end();
}

void PageFanetGround::draw_extra() {
  uint8_t* graphic = nullptr;
  switch (mode) {
    case FanetGroundTrackingMode::WALKING:
      graphic = (uint8_t*)fanet_walking_bmp;
      break;
    case FanetGroundTrackingMode::LANDED_OK:
      graphic = (uint8_t*)fanet_landedok_bmp;
      break;
    case FanetGroundTrackingMode::VEHICLE:
      graphic = (uint8_t*)fanet_vehicle_bmp;
      break;
    case FanetGroundTrackingMode::NEED_RIDE:
      graphic = (uint8_t*)fanet_needride_bmp;
      break;
  }

  constexpr int Y_OFFSET = 30;
  constexpr auto LINE_HEIGHT = 10;
  constexpr int IMG_SIZE = 96;

  if (graphic) {
    // TODO:  Fancy, animated icons
    u8g2.drawXBM(0, Y_OFFSET, IMG_SIZE, IMG_SIZE, graphic);
  }

  auto y_offset = Y_OFFSET + IMG_SIZE + 10;
  display_GPS_icon(70, y_offset + LINE_HEIGHT * 2 - 3);
  u8g2.setCursor(3, y_offset += LINE_HEIGHT);
  u8g2.setFont(leaf_5x8);
  u8g2.println((String) "lat: " + gps.location.lat());
  u8g2.setCursor(3, y_offset += LINE_HEIGHT);
  u8g2.println((String) "lng: " + gps.location.lng());

  // Update the ground station for our position
  if (gps.location.isValid()) {
    // Update the FANet radio module of our current location...
    // This really, really shouldn't be done in a Display object
    // but whatever, we'll refactor later.
    // TODO:  Delete me
    FanetRadio::getInstance().setCurrentLocation(gps.location.lat(), gps.location.lng(),
                                                 gps.altitude.meters(), gps.course.deg(),
                                                 baro.climbRate / 100.0f, gps.speed.kmph());
  }
}

PageFanetGround& PageFanetGround::getInstance() {
  static PageFanetGround instance;
  return instance;
}
