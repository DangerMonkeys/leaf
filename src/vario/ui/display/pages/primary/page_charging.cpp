#include <Arduino.h>
#include <U8g2lib.h>

#include "comms/leaf_log_sync.h"
#include "hardware/buttons.h"
#include "power.h"
#include "storage/sd_card.h"
#include "system/version_info.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

namespace {
  constexpr uint8_t LEAF_LOG_DIALOG_WIDTH = 96;
  constexpr uint8_t LEAF_LOG_DIALOG_Y = 94;
  constexpr uint8_t LEAF_LOG_DIALOG_HEIGHT = 67;
  constexpr uint8_t LEAF_LOG_DIALOG_SEPARATOR_HEIGHT = 4;

  void printCentered(const char* text, uint8_t baselineY) {
    int16_t x = (LEAF_LOG_DIALOG_WIDTH - u8g2.getStrWidth(text)) / 2;
    if (x < 0) x = 0;
    u8g2.setCursor(x, baselineY);
    u8g2.print(text);
  }
}  // namespace

// CHARGING PAGE
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

    // Battery Stats
    u8g2.setFont(leaf_5x8);
    uint8_t yPos = 124;
    if (info.batteryPercent > 22) {
      u8g2.setDrawColor(0);
    } else {
      u8g2.setDrawColor(1);
      yPos = 35;
    }
    u8g2.setCursor(35, yPos);
#ifdef ISET
    u8g2.print(info.chargeCurrentMA);
    u8g2.print("mA ");
#endif
    u8g2.setCursor(30, yPos + 10);
    u8g2.print(info.batteryMV);
    u8g2.print("mV");
    u8g2.setDrawColor(1);

    if (settings.dev_mode) {
      // If Developer Mode enabled, show Input Current max limit
      u8g2.setFont(leaf_6x12);
      u8g2.setCursor(10, 157);
      u8g2.print("Limit: ");
      if (info.inputCurrent == PowerInputLevel::i100mA)
        u8g2.print("100mA");
      else if (info.inputCurrent == PowerInputLevel::i500mA)
        u8g2.print("500mA");
      else if (info.inputCurrent == PowerInputLevel::Max)
        u8g2.print("810mA");
      else if (info.inputCurrent == PowerInputLevel::Standby)
        u8g2.print(" OFF");
    }

    // Display the current SW version
    u8g2.setCursor(0, 172);
    u8g2.setFont(leaf_5x8);
    u8g2.print("v");
    u8g2.print(LeafVersionInfo::firmwareVersion());

    // SD Card Mounted
    u8g2.setCursor(12, 191);
    u8g2.setFont(leaf_icons);
    if (!sdcard.isCardPresent()) {
      u8g2.print((char)61);
      u8g2.setFont(leaf_6x12);
      u8g2.print(" NO SD!");
    } else {
      u8g2.print((char)60);
    }

    if (leafLogSync.screenActive()) {
      u8g2.setDrawColor(0);
      u8g2.drawBox(0, LEAF_LOG_DIALOG_Y - LEAF_LOG_DIALOG_SEPARATOR_HEIGHT, LEAF_LOG_DIALOG_WIDTH,
                   LEAF_LOG_DIALOG_SEPARATOR_HEIGHT);
      u8g2.setDrawColor(1);
      u8g2.drawRBox(0, LEAF_LOG_DIALOG_Y, LEAF_LOG_DIALOG_WIDTH, LEAF_LOG_DIALOG_HEIGHT, 3);
      u8g2.setDrawColor(0);
      u8g2.setFont(leaf_6x12);
      printCentered("Leaf Log", 109);

      u8g2.setFont(leaf_5x8);
      char status[32];
      if (leafLogSync.retryPending() || leafLogSync.powerOnPending()) {
        snprintf(status, sizeof(status), "%s", leafLogSync.statusLine());
      } else if (leafLogSync.progressKnown() && leafLogSync.totalCount() > 0) {
        snprintf(status, sizeof(status), "Uploading %u of %u", leafLogSync.currentCount(),
                 leafLogSync.totalCount());
      } else {
        snprintf(status, sizeof(status), "%s", leafLogSync.statusLine());
      }
      printCentered(status, 124);
      if (leafLogSync.powerOnPending()) {
        printCentered("Please wait...", 148);
      } else if (leafLogSync.massStorageUnavailable()) {
        printCentered("Press to retry.", 143);
        printCentered("Hold to turn on.", 155);
      } else {
        printCentered("Press to cancel", 138);
        printCentered("and open USB drive.", 148);
        printCentered("Hold to turn on.", 158);
      }
      u8g2.setDrawColor(1);
    }

  } while (u8g2.nextPage());
}

void chargingPage_button(Button button, ButtonEvent state, uint8_t count) {
  if (button == Button::CENTER && state == ButtonEvent::PRESSED &&
      leafLogSync.interceptsChargingButtons()) {
    leafLogSync.requestCancel();
    display.update();
    return;
  }
  if (button == Button::CENTER && state == ButtonEvent::HELD) {
    leafLogSync.requestPowerOn();
    buttons.consumeButton();
    display.update();
    return;
  }
  switch (button) {
    case Button::CENTER:
      break;
    case Button::UP:
      switch (state) {
        case ButtonEvent::CLICKED:
          break;
        case ButtonEvent::HELD:
          if (settings.dev_mode) {
            power.increaseInputCurrent();
            speaker.playSound(fx::enter);
          }
          break;
      }
      break;
    case Button::DOWN:
      switch (state) {
        case ButtonEvent::CLICKED:
          break;
        case ButtonEvent::HELD:
          if (settings.dev_mode) {
            power.decreaseInputCurrent();
            speaker.playSound(fx::exit);
          }
          break;
      }
      break;
  }
  display.update();
}
