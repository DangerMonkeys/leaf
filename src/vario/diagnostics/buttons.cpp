#include "diagnostics/buttons.h"

#include "etl/message_router.h"

#include "dispatch/message_types.h"

ButtonMonitor buttonMonitor;

void ButtonMonitor::on_receive(const ButtonEventMessage& msg) {
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

  switch (msg.event) {
    case ButtonEvent::PRESSED:
      Serial.print(" state: PRESSED    ");
      break;
    case ButtonEvent::CLICKED:
      Serial.print(" state: CLICKED    ");
      break;
    case ButtonEvent::RELEASED:
      Serial.print(" state: RELEASED   ");
      break;
    case ButtonEvent::HELD:
      Serial.print(" state: HELD       ");
      break;
    case ButtonEvent::HELD_LONG:
      Serial.print(" state: HELD_LONG  ");
      break;
    case ButtonEvent::INCREMENTED:
      Serial.print(" state: INCREMENTED");
  }

  Serial.print(" hold count: ");
  Serial.println(msg.holdCount);
}
