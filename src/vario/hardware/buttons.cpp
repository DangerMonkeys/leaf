/*
 * buttons.cpp
 *
 * 5-way joystick switch (UP DOWN LEFT RIGHT CENTER)
 * Button are active-high, via resistor dividers fed from "SYSPOWER", which is typically 4.4V
 * (supply from Battery Charger under USB power) or Battery voltage (when no USB power is applied)
 * Use internal pull-down resistors on button pins
 *
 */
#include "hardware/buttons.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "diagnostics/cpu_utilization.h"
#include "hardware/configuration.h"
#include "instruments/baro.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/menu_page.h"
#include "ui/display/pages.h"
#include "ui/display/pages/dialogs/page_warning.h"
#include "ui/display/pages/primary/page_charging.h"
#include "ui/display/pages/primary/page_debug.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/display/pages/primary/page_simple.h"
#include "ui/display/pages/primary/page_thermal.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "utils/magic_enum.h"

Buttons buttons;

// time in ms for stabilized button state before returning the button press
const uint32_t DEBOUNCE_TIME_MS = 5;

// time in ms to count a button as held down
const uint16_t HOLD_TIME_MS = 800;

// additional time in ms to start further actions on long-holds
const uint16_t HOLD_LONG_TIME_MS = 3500 - HOLD_TIME_MS;

// time in ms between increment events while holding the button
const uint16_t INCREMENT_TIME_MS = 500;

namespace {
  constexpr uint64_t BUTTON_WAKE_MASK = (1ULL << BUTTON_PIN_CENTER) | (1ULL << BUTTON_PIN_LEFT) |
                                        (1ULL << BUTTON_PIN_RIGHT) | (1ULL << BUTTON_PIN_UP) |
                                        (1ULL << BUTTON_PIN_DOWN);
}

void ARDUINO_ISR_ATTR Buttons::onPressInterrupt(void* arg) {
  auto* self = static_cast<Buttons*>(arg);
  if (self->urgentPressArmed_.load(std::memory_order_relaxed)) {
    self->urgentPressLatched_.store(true, std::memory_order_release);
  }
}

Button Buttons::init() {
  // configure pins
  pinMode(BUTTON_PIN_UP, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_DOWN, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_LEFT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_RIGHT, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_CENTER, INPUT_PULLDOWN);

  attachInterruptArg(BUTTON_PIN_UP, onPressInterrupt, this, RISING);
  attachInterruptArg(BUTTON_PIN_DOWN, onPressInterrupt, this, RISING);
  attachInterruptArg(BUTTON_PIN_LEFT, onPressInterrupt, this, RISING);
  attachInterruptArg(BUTTON_PIN_RIGHT, onPressInterrupt, this, RISING);
  attachInterruptArg(BUTTON_PIN_CENTER, onPressInterrupt, this, RISING);

  return inspectPins();
}

void Buttons::armUrgentPressCapture() {
  urgentPressLatched_.store(false, std::memory_order_release);
  urgentPressArmed_.store(true, std::memory_order_release);
}

void Buttons::disarmUrgentPressCapture() {
  urgentPressArmed_.store(false, std::memory_order_release);
}

void Buttons::suppressEventsUntilRelease() {
  state_ = State::Up;
  currentButton_ = Button::NONE;
  holdCounter_ = 0;
  consumed_ = false;
  suppressUntilRelease_ = inspectPins() != Button::NONE;
}

void Buttons::enableSleepWakeFromAnyButton() {
  const esp_err_t error = esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
  if (error != ESP_OK) {
    Serial.printf("Button ext1 sleep wake configuration failed: %d\n", error);
  }
}

void Buttons::report(Button button, ButtonEvent state) {
  cpu_utilization::recordButtonEvent(button, state);

  etl::imessage_bus* bus = bus_;
  if (bus) {
    bus->receive(ButtonEventMessage(button, state, holdCounter_));
  }
}

void Buttons::startDebouncing(Button button) {
  state_ = State::Debouncing;
  timeInitial_ = millis();
  currentButton_ = button;
}

void Buttons::update() {
  Button button = inspectPins();

  if (suppressUntilRelease_) {
    if (button == Button::NONE) {
      suppressUntilRelease_ = false;
      state_ = State::Up;
      currentButton_ = Button::NONE;
      consumed_ = false;
    }
    return;
  }

  if (state_ == State::Up) {
    if (button != Button::NONE) {
      startDebouncing(button);
    }

  } else if (state_ == State::Debouncing) {
    if (button == Button::NONE) {
      state_ = State::Up;
    } else if (button != currentButton_) {
      startDebouncing(button);
    } else if (millis() - timeInitial_ >= DEBOUNCE_TIME_MS) {
      state_ = State::Down;
      consumed_ = false;
      timeInitial_ = millis();
      report(button, ButtonEvent::PRESSED);
    }

  } else if (state_ == State::Down) {
    if (button == Button::NONE) {
      state_ = State::Up;
      if (!consumed_) report(currentButton_, ButtonEvent::CLICKED);
      report(currentButton_, ButtonEvent::RELEASED);
    } else if (button != currentButton_) {
      Button released = currentButton_;
      startDebouncing(button);
      if (!consumed_) report(released, ButtonEvent::CLICKED);
      report(released, ButtonEvent::RELEASED);
    } else if (millis() - timeInitial_ >= HOLD_TIME_MS) {
      state_ = State::Held;
      holdCounter_ = 0;
      timeInitial_ = millis();
      timeLastIncrement_ = timeInitial_;
      if (!consumed_) report(button, ButtonEvent::HELD);
      if (!consumed_) report(button, ButtonEvent::INCREMENTED);
    }

  } else if (state_ == State::Held) {
    if (button == Button::NONE) {
      state_ = State::Up;
      holdCounter_ = 0;
      report(currentButton_, ButtonEvent::RELEASED);
    } else if (button != currentButton_) {
      Button released = currentButton_;
      startDebouncing(button);
      holdCounter_ = 0;
      report(released, ButtonEvent::RELEASED);
    } else if (millis() - timeLastIncrement_ >= INCREMENT_TIME_MS) {
      holdCounter_++;
      timeLastIncrement_ += INCREMENT_TIME_MS;
      if (!consumed_) report(button, ButtonEvent::INCREMENTED);
    } else if (millis() - timeInitial_ >= HOLD_LONG_TIME_MS) {
      state_ = State::HeldLong;
      if (!consumed_) report(button, ButtonEvent::HELD_LONG);
    }

  } else if (state_ == State::HeldLong) {
    if (button == Button::NONE) {
      state_ = State::Up;
      holdCounter_ = 0;
      report(currentButton_, ButtonEvent::RELEASED);
    } else if (button != currentButton_) {
      Button released = currentButton_;
      startDebouncing(button);
      holdCounter_ = 0;
      report(released, ButtonEvent::RELEASED);
    } else if (millis() - timeLastIncrement_ >= INCREMENT_TIME_MS) {
      holdCounter_++;
      timeLastIncrement_ += INCREMENT_TIME_MS;
      if (!consumed_) report(button, ButtonEvent::INCREMENTED);
    }

  } else {
    fatalError("Buttons::update found unsupported state '%s' (%u)", nameOf(state_).c_str(), state_);
  }
}

Button Buttons::inspectPins() {
  Button button = Button::NONE;
  if (digitalRead(BUTTON_PIN_CENTER) == HIGH)
    button = Button::CENTER;
  else if (digitalRead(BUTTON_PIN_DOWN) == HIGH)
    button = Button::DOWN;
  else if (digitalRead(BUTTON_PIN_LEFT) == HIGH)
    button = Button::LEFT;
  else if (digitalRead(BUTTON_PIN_RIGHT) == HIGH)
    button = Button::RIGHT;
  else if (digitalRead(BUTTON_PIN_UP) == HIGH)
    button = Button::UP;
  return button;
}
