#include "diagnostics/buttons.h"

#include "etl/message_router.h"

#include "dispatch/message_types.h"

ButtonMonitor buttonMonitor;

void ButtonMonitor::on_receive(const ButtonEvent& msg) {
  switch (msg.button) {
    case Button::CENTER:
      Serial.print("button: CENTER");
      break;
    case Button::LEFT:
      Serial.print("button: LEFT  ");
      break;
    case Button::RIGHT:
      Serial.print("button: RIGHT ");
      break;
    case Button::UP:
      Serial.print("button: UP    ");
      break;
    case Button::DOWN:
      Serial.print("button: DOWN  ");
      break;
    case Button::NONE:
      Serial.print("button: NONE  ");
      break;
  }

  switch (msg.state) {
    case PRESSED:
      Serial.print(" state: PRESSED  ");
      break;
    case RELEASED:
      Serial.print(" state: RELEASED ");
      break;
    case HELD:
      Serial.print(" state: HELD     ");
      break;
    case HELD_LONG:
      Serial.print(" state: HELD_LONG");
  }

  Serial.print(" hold count: ");
  Serial.println(msg.holdCount);
}
