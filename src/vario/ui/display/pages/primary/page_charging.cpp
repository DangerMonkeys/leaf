#include <Arduino.h>
#include <U8g2lib.h>

#include "charging_dialog_art.h"
#include "comms/leaf_log_sync.h"
#include "hardware/buttons.h"
#include "power.h"
#include "storage/sd_card.h"
#include "system/usb_state.h"
#include "system/version_info.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

namespace {
  constexpr uint8_t CHARGING_DIALOG_WIDTH = 96;
  constexpr uint8_t CHARGING_DIALOG_Y = 141;
  constexpr uint8_t CHARGING_DIALOG_HEIGHT = 51;
  constexpr uint8_t SMALL_DIALOG_HEIGHT = 23;
  // Extend the centered 22px panel upward by one row for equal text margins.
  // Its bottom edge and contents stay fixed; the version footer starts at 184.
  constexpr uint8_t SMALL_DIALOG_Y = 150;
  constexpr uint8_t FOOTER_BASELINE_Y = 192;

  void printCentered(const char* text, uint8_t baselineY) {
    int16_t x = (CHARGING_DIALOG_WIDTH - u8g2.getStrWidth(text)) / 2;
    if (x < 0) x = 0;
    u8g2.setCursor(x, baselineY);
    u8g2.print(text);
  }

  void glyph(const charging_art::Glyph& art, bool transparent = false) {
    u8g2.setBitmapMode(transparent);
    u8g2.drawXBMP(art.x, CHARGING_DIALOG_Y + art.y, art.width, art.height, art.pixels);
    u8g2.setBitmapMode(0);
  }

  void dialogBase(uint8_t yTop, uint8_t height) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, yTop, 96, height);
    u8g2.setDrawColor(0);
    // Two-pixel corner cutouts, copied from the reference panel.
    for (uint8_t y : {yTop, uint8_t(yTop + height - 1)}) {
      u8g2.drawBox(0, y, 2, 1);
      u8g2.drawBox(94, y, 2, 1);
    }
    for (uint8_t y : {uint8_t(yTop + 1), uint8_t(yTop + height - 2)}) {
      u8g2.drawPixel(0, y);
      u8g2.drawPixel(95, y);
    }
  }

  void smallDialog(const char* line1, const char* line2 = nullptr, bool usb = false) {
    dialogBase(SMALL_DIALOG_Y, SMALL_DIALOG_HEIGHT);
    // Keep the reference USB icon at its existing screen position.
    if (usb) {
      const auto& icon = charging_art::usb_icon;
      u8g2.drawXBMP(icon.x, SMALL_DIALOG_Y + 1 + icon.y, icon.width, icon.height, icon.pixels);
    }
    u8g2.setFont(leaf_5x8);
    const int16_t center = usb ? 70 : 48;
    const auto line = [&](const char* text, uint8_t y) {
      u8g2.setCursor(center - u8g2.getStrWidth(text) / 2, y);
      u8g2.print(text);
    };
    line(line1, SMALL_DIALOG_Y + (line2 ? 10 : 15));
    if (line2) line(line2, SMALL_DIALOG_Y + 20);
    u8g2.setDrawColor(1);
  }

  void leafLogBranding(uint8_t x, uint8_t y, bool includeWordmark = true) {
    // Share the large panel's leaf/wordmark offsets with the compact panel.
    if (includeWordmark) {
      const auto& wordmark = charging_art::wordmark;
      u8g2.drawXBMP(x + wordmark.x - 30, y + wordmark.y, wordmark.width, wordmark.height,
                    wordmark.pixels);
    }
    u8g2.setFont(leaf_8x14);
    u8g2.drawGlyph(x, y + 16, 140);
  }

  void leafLogDialog() {
    using Phase = LeafLogSync::ScreenPhase;
    const auto phase = leafLogSync.screenPhase();
    static Phase lastPhase = Phase::Scanning;
    static uint32_t phaseTick = millis() / 500;
    if (phase != lastPhase) {
      lastPhase = phase;
      phaseTick = millis() / 500;
    }
    const uint32_t tick = millis() / 500 - phaseTick;
    const bool confirming = leafLogSync.cancelConfirmation();
    dialogBase(CHARGING_DIALOG_Y, CHARGING_DIALOG_HEIGHT);
    leafLogBranding(30, CHARGING_DIALOG_Y);
    if (confirming) {
      u8g2.setFont(leaf_5x8);
      printCentered("Cancel Uploads?", 178);
    } else {
      if (phase == Phase::Transfer && tick % 11 < 10) {
        // Existing hollow leaf, moving four pixels every 500ms. Draw it behind
        // the device and cloud; the final frame is fully hidden in the cloud.
        u8g2.drawGlyph(21 + 4 * (tick % 11), CHARGING_DIALOG_Y + 39, 142);
      }
      glyph(charging_art::device);
      if (phase == Phase::Connecting || phase == Phase::Transfer || phase == Phase::Complete ||
          phase == Phase::Paused) {
        // Preserve the cloud's shaped edge while it covers the moving leaf.
        glyph(charging_art::cloud, true);
      }
      switch (phase) {
        case Phase::Scanning:
          u8g2.drawBox(17, CHARGING_DIALOG_Y + 27 + 2 * (tick % 7), 8, 2);
          break;
        case Phase::Wifi:
          if (tick % 4 >= 1) glyph(charging_art::wifi_1, true);
          if (tick % 4 >= 2) glyph(charging_art::wifi_2, true);
          if (tick % 4 >= 3) glyph(charging_art::wifi_3, true);
          break;
        case Phase::Connecting:
          u8g2.drawBox(30, CHARGING_DIALOG_Y + 33, 2, 3);
          u8g2.drawBox(54, CHARGING_DIALOG_Y + 33, 2, 3);
          if (tick % 4 != 0)
            u8g2.drawHLine(32, CHARGING_DIALOG_Y + 34, 7 * (tick % 4) + (tick % 4 == 3));
          break;
        case Phase::Transfer:
          break;
        case Phase::Complete:
          glyph(leafLogSync.sessionProblem() ? charging_art::failure : charging_art::checkmark);
          break;
        case Phase::Paused:
          for (uint8_t dot = 0; dot < tick % 4; ++dot) {
            u8g2.drawBox(36 + dot * 7, CHARGING_DIALOG_Y + 33, 2, 2);
          }
          break;
      }
    }
    if (!confirming && (phase == Phase::Transfer || phase == Phase::Complete) &&
        leafLogSync.progressKnown() && leafLogSync.totalCount() > 0) {
      char count[12];
      snprintf(count, sizeof(count), "%u/%u", leafLogSync.currentCount(), leafLogSync.totalCount());
      u8g2.setFont(leaf_5x8);
      // Keep the longest allowed 100/100 on screen, one row above the original reference.
      u8g2.setCursor(73 - u8g2.getStrWidth(count) / 2, 190);
      u8g2.print(count);
    }
    u8g2.setDrawColor(1);
  }

  void compactStatusDialog(bool usb, bool leafLog) {
    if (!leafLog) {
      if (usb) smallDialog("USB Drive", "Available", true);
      return;
    }
    dialogBase(SMALL_DIALOG_Y, SMALL_DIALOG_HEIGHT);
    if (usb) {
      const auto& icon = charging_art::usb_icon;
      u8g2.drawXBMP(icon.x, SMALL_DIALOG_Y + 1 + icon.y, icon.width, icon.height, icon.pixels);
      u8g2.drawBox(47, SMALL_DIALOG_Y, 2, SMALL_DIALOG_HEIGHT);
    }
    leafLogBranding(usb ? 54 : 21, SMALL_DIALOG_Y + 2, !usb);
    const bool problem = leafLogSync.result() == LeafLogSync::Result::Problem;
    const auto& result = problem ? charging_art::failure : charging_art::checkmark;
    // Center either result glyph at the same position in its available space.
    const uint8_t x = (usb ? 82 : 72) - result.width / 2;
    u8g2.drawXBMP(x, SMALL_DIALOG_Y + 5, result.width, result.height, result.pixels);
    u8g2.setDrawColor(1);
  }
}  // namespace

// CHARGING PAGE
void chargingPage_draw() {
  const auto& info = power.info();
  const bool usbAvailable = sdcard.hostOwnsMassStorage() && leaf_usb::hostMounted();
  const bool leafLogResult =
      settings.labs_leafLog && leafLogSync.result() != LeafLogSync::Result::None;
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

    if (settings.dev_mode && !leafLogSync.screenActive() && !sdcard.hostOwnsMassStorage() &&
        !leafLogSync.cancelPending() && !leafLogResult) {
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

    // A present card needs no status icon. Use the bottom line for the combined
    // firmware/hardware version, or replace it with an explicit missing-card warning.
    if (!sdcard.isCardPresent()) {
      u8g2.setCursor(0, FOOTER_BASELINE_Y);
      u8g2.setFont(leaf_icons);
      u8g2.print((char)61);
      u8g2.setCursor(15, FOOTER_BASELINE_Y);
      u8g2.setFont(leaf_6x12);
      u8g2.print("NO SD CARD!");
    } else {
      u8g2.setCursor(0, FOOTER_BASELINE_Y);
      u8g2.setFont(leaf_5x8);
      u8g2.print("v");
      u8g2.print(LeafVersionInfo::firmwareVersion());
    }

    if (leafLogSync.powerOnPending()) {
      if (leafLogSync.powerOnClosingUsb())
        smallDialog("Closing", "USB Drive", true);
      else
        smallDialog("Turning On");
    } else if (leafLogSync.cancelPending()) {
      smallDialog("Opening", "USB Drive", true);
    } else if (leafLogSync.massStorageUnavailable() && leafLogSync.screenActive()) {
      smallDialog("USB Drive", "Not Ready", true);
    } else if (leafLogSync.screenActive()) {
      leafLogDialog();
    } else {
      compactStatusDialog(usbAvailable, leafLogResult);
    }
  } while (u8g2.nextPage());
}

void chargingPage_button(Button button, ButtonEvent state, uint8_t count) {
  if (button == Button::CENTER && state == ButtonEvent::CLICKED &&
      leafLogSync.interceptsChargingButtons()) {
    leafLogSync.requestChargingClick();
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
