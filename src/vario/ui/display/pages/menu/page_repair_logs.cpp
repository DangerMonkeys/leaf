#include "ui/display/pages/menu/page_repair_logs.h"

#include "logbook/logbook_store.h"
#include "logging/log.h"
#include "ui/display/display.h"
#include "ui/display/message_box.h"
#include "ui/display/pages.h"
#include "ui/display/pages/menu/page_menu_log.h"

void PageRepairLogs::setPhase(Phase phase) {
  phase_ = phase;
  drawn_ = false;
}

void PageRepairLogs::start() {
  blocked_ = flightTimer_isRunning();
  ownsRepair_ = !blocked_ && LogbookStore::beginRepairSession();
  busy_ = !blocked_ && !ownsRepair_;
  setPhase(blocked_ || busy_ ? Phase::Result : Phase::Checking);
  push_page(this);
}

void PageRepairLogs::closed(bool removed) {
  if (removed && ownsRepair_) LogbookStore::beginRepairSession(false);
}

bool PageRepairLogs::button_event(Button button, ButtonEvent event, uint8_t) {
  if (phase_ != Phase::Result && event == ButtonEvent::CLICKED &&
      (button == Button::LEFT || button == Button::CENTER)) {
    // Stop work immediately; draw() still preserves the progress dialog minimum.
    LogbookStore::beginRepairSession(false);
  }
  return true;
}

void PageRepairLogs::draw() {
  auto status = LogbookStore::repairStatus();
  // Extra draws caused by button events do not shorten the minimum readable interval.
  if (drawn_ && millis() - visibleSince_ >= 1000) {
    if (phase_ == Phase::Checking && status.state == LogRepairState::Ready) {
      if (LogbookStore::startRepair()) setPhase(Phase::Repairing);
    } else if (phase_ != Phase::Result && !LogbookStore::repairPending()) {
      setPhase(Phase::Result);
    } else if (phase_ == Phase::Result) {
      pop_page();
      logMenuPage.draw();
      return;
    }
  }
  status = LogbookStore::repairStatus();
  const char* first;
  const char* second;
  if (phase_ == Phase::Checking) {
    first = "Checking";
    second = "logs...";
  } else if (phase_ == Phase::Repairing) {
    first = "Repairing";
    second = "logs...";
  } else if (busy_) {
    first = "Logs busy";
    second = "try again";
  } else if (blocked_) {
    first = "Stop flight";
    second = "to repair logs";
  } else if (status.state == LogRepairState::Cancelled) {
    first = "Repair";
    second = "cancelled";
  } else if (status.state == LogRepairState::Failed) {
    first = "Unable to";
    second = "repair logs";
  } else if (status.unresolved) {
    first = "Some logs";
    second = "not repaired";
  } else if (status.problems) {
    first = "Repair";
    second = "complete";
  } else {
    first = "No problems";
    second = "found";
  }
  u8g2.firstPage();
  do {
    message_box::draw(first, second);
  } while (u8g2.nextPage());
  if (!drawn_) {
    visibleSince_ = millis();
    drawn_ = true;
  }
}
