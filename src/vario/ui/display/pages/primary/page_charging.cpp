#include <Arduino.h>
#include <U8g2lib.h>

#include "hardware/buttons.h"
#include "leaf_version.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

/*********************************************************************************
**   CHARGING PAGE    ************************************************************
*********************************************************************************/
void chargingPage_draw() {
  const auto& info = power.info();
  u8g2.firstPage();
  do {
    // Battery Percent
    uint8_t fontOffset = 3;
    if (info.batteryPercent == 100) fontOffset = 0;
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(36 + fontOffset, 12);
    u8g2.print(info.batteryPercent);
    u8g2.print('%');

    display_batt_charging_fullscreen(48, 17);

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(5, 157);
    if (info.inputCurrent == PowerInputLevel::i100mA)
      u8g2.print("100mA");
    else if (info.inputCurrent == PowerInputLevel::i500mA)
      u8g2.print("500mA");
    else if (info.inputCurrent == PowerInputLevel::Max)
      u8g2.print("810mA");
    else if (info.inputCurrent == PowerInputLevel::Standby)
      u8g2.print(" OFF");

    u8g2.print(" ");
    u8g2.print(info.batteryMV);
    u8g2.print("mV");

    // Display the current version
    u8g2.setCursor(0, 172);
    u8g2.setFont(leaf_5x8);
    u8g2.print("v");
    u8g2.print(FIRMWARE_VERSION);

    // SD Card Mounted
    u8g2.setCursor(12, 191);
    u8g2.setFont(leaf_icons);
    if (!sdcard.isMounted()) {
      u8g2.print((char)61);
      u8g2.setFont(leaf_6x12);
      u8g2.print(" NO SD!");
    } else {
      u8g2.print((char)60);
    }

  } while (u8g2.nextPage());
}

void chargingPage_button(Button button, ButtonEvent state, uint8_t count) {
  switch (button) {
    case Button::CENTER:
      if (state == ButtonEvent::INCREMENTED && count == 1) {
        buttons.consumeButton();
        display.clear();
        display.showOnSplash();
        display.setPage(MainPage::Thermal);  // TODO: set initial page to the user's last used page
        speaker.playSound(fx::enter);
        power.switchToOnState();
      }
      break;
    case Button::UP:
      switch (state) {
        case ButtonEvent::CLICKED:
          break;
        case ButtonEvent::HELD:
          power.increaseInputCurrent();

          speaker.playSound(fx::enter);
          break;
      }
      break;
    case Button::DOWN:
      switch (state) {
        case ButtonEvent::CLICKED:
          break;
        case ButtonEvent::HELD:
          power.decreaseInputCurrent();
          speaker.playSound(fx::exit);
          break;
      }
      break;
  }
  display.update();
}