#include <Arduino.h>
#include <U8g2lib.h>

#include "hardware/buttons.h"
#include "hardware/icm_20948.h"
#include "instruments/imu.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/display/pages/primary/page_debug2.h"
#include "ui/input/buttons.h"

namespace {
  uint32_t percent(uint32_t numerator, uint32_t denominator) {
    if (denominator == 0) return 0;
    return (uint32_t)(((uint64_t)numerator * 100 + denominator / 2) / denominator);
  }

  void printCounterLine(uint8_t x, uint8_t& y, const char* label, uint32_t count, uint32_t pct,
                        bool showPercent = true) {
    u8g2.setCursor(x, y);
    u8g2.print(label);
    u8g2.print(':');
    u8g2.print(count);
    if (showPercent) {
      u8g2.print(' ');
      u8g2.print(pct);
      u8g2.print('%');
    }
    y += 8;
  }
}  // namespace

void debug2Page_draw() {
  u8g2.firstPage();
  do {
    const ICM20948& icm = ICM20948::getInstance();
    const uint32_t updateCalls = icm.imuUpdateCallCount();
    const uint32_t fifoPackets = icm.motionFifoPacketCount();
    const uint32_t busSamples = imu.motionSampleCount();
    const uint32_t gravityCandidates = imu.gravityUpdateCandidateCount();
    const uint32_t gravityAccepted = imu.gravityUpdateAcceptedCount();

    uint8_t x = 0;
    uint8_t y = 8;
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(x, y);
    u8g2.print("IMU FIFO");
    y += 8;
    u8g2.setFont(leaf_5x8);
    printCounterLine(x, y, "upd", updateCalls, 0, false);
    printCounterLine(x, y, "empty", icm.fifoNoDataCount(),
                     percent(icm.fifoNoDataCount(), updateCalls));
    printCounterLine(x, y, "fifo", fifoPackets, percent(fifoPackets, updateCalls));
    printCounterLine(x, y, "match", icm.motionMatchedPacketCount(),
                     percent(icm.motionMatchedPacketCount(), fifoPackets));
    printCounterLine(x, y, "mis", icm.motionMismatchedPacketCount(),
                     percent(icm.motionMismatchedPacketCount(), fifoPackets));
    printCounterLine(x, y, "bus", icm.motionPublishedSampleCount(),
                     percent(icm.motionPublishedSampleCount(), fifoPackets));
    printCounterLine(x, y, "rst", icm.fifoResetCount(), 0, false);

    y += 4;
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(x, y);
    u8g2.print("IMU USE");
    y += 8;
    u8g2.setFont(leaf_5x8);
    printCounterLine(x, y, "rx", busSamples, percent(busSamples, icm.motionPublishedSampleCount()));
    printCounterLine(x, y, "baro!", imu.motionSampleBaroNotReadyCount(),
                     percent(imu.motionSampleBaroNotReadyCount(), busSamples));
    printCounterLine(x, y, "field!", imu.motionSampleMissingFieldsCount(),
                     percent(imu.motionSampleMissingFieldsCount(), busSamples));
    printCounterLine(x, y, "quat!", imu.motionSampleRejectedQuaternionCount(),
                     percent(imu.motionSampleRejectedQuaternionCount(), busSamples));
    printCounterLine(x, y, "proc", imu.motionSampleProcessedCount(),
                     percent(imu.motionSampleProcessedCount(), busSamples));
    printCounterLine(x, y, "kal", imu.kalmanUpdateSampleCount(),
                     percent(imu.kalmanUpdateSampleCount(), busSamples));

    y += 4;
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(x, y);
    u8g2.print("GRAV ");
    u8g2.print(imu.gravityEstimate(), 3);
    u8g2.print("g");
    y += 8;
    u8g2.setFont(leaf_5x8);
    printCounterLine(x, y, "init", imu.gravityInitSampleCount(), 0, false);
    printCounterLine(x, y, "cand", gravityCandidates, 0, false);
    printCounterLine(x, y, "use", gravityAccepted, percent(gravityAccepted, gravityCandidates));
    printCounterLine(x, y, "accel!", imu.gravityUpdateRejectedAccelCount(),
                     percent(imu.gravityUpdateRejectedAccelCount(), gravityCandidates));
    printCounterLine(x, y, "vert!", imu.gravityUpdateRejectedVerticalCount(),
                     percent(imu.gravityUpdateRejectedVerticalCount(), gravityCandidates));
    printCounterLine(x, y, "slew", imu.gravityUpdateSlewLimitedCount(),
                     percent(imu.gravityUpdateSlewLimitedCount(), gravityAccepted));
    printCounterLine(x, y, "time!", imu.gravityUpdateRejectedTimeCount(),
                     percent(imu.gravityUpdateRejectedTimeCount(), gravityCandidates));
    printCounterLine(x, y, "plaus!", imu.gravityUpdateRejectedPlausibilityCount(),
                     percent(imu.gravityUpdateRejectedPlausibilityCount(), gravityCandidates));
  } while (u8g2.nextPage());
}

void debug2Page_button(Button button, ButtonEvent state, uint8_t count) {
  switch (button) {
    case Button::CENTER:
      switch (state) {
        case ButtonEvent::INCREMENTED:
          if (count == 2) {
            power.shutdown();
            while (buttons.inspectPins() == Button::CENTER) {
            }
            display.setPage(MainPage::Charging);
          }
          break;
        case ButtonEvent::CLICKED:
          display.turnPage(PageAction::Home);
          break;
      }
      break;
    case Button::RIGHT:
      if (state == ButtonEvent::CLICKED) {
        display.turnPage(PageAction::Next);
        speaker.playSound(fx::increase);
      }
      break;
    case Button::LEFT:
      if (state == ButtonEvent::CLICKED) {
        display.turnPage(PageAction::Prev);
        speaker.playSound(fx::decrease);
      }
      break;
  }
  display.update();
}
