// Arduino core entry points, wired to the virtual clock and the virtual board.

#include <Arduino.h>

#include <random>

#include "sim/board.h"
#include "sim/clock.h"

// ---------------------------------------------------------------- time

uint32_t millis(void) { return sim::clock().millis(); }
uint32_t micros(void) { return sim::clock().micros(); }
uint64_t esp_timer_get_time(void) { return sim::clock().nowUs(); }

void delay(uint32_t ms) { sim::clock().advanceUs((uint64_t)ms * 1000); }
void delayMicroseconds(uint32_t us) { sim::clock().advanceUs(us); }

// The firmware yields inside a few wait loops.  Letting a yield cost a small slice of virtual
// time is what keeps those loops from spinning forever against a clock that never moves.
void yield(void) { sim::clock().advanceUs(50); }
extern "C" void vPortYield(void) { yield(); }

void configTime(long gmtOffsetSec, int daylightOffsetSec, const char* server1, const char* server2,
                const char* server3) {
  (void)gmtOffsetSec;
  (void)daylightOffsetSec;
  (void)server1;
  (void)server2;
  (void)server3;
}

// ---------------------------------------------------------------- pins

void pinMode(uint8_t pin, uint8_t mode) { sim::board().pinMode(pin, mode); }
void digitalWrite(uint8_t pin, uint8_t value) { sim::board().digitalWrite(pin, value); }
int digitalRead(uint8_t pin) { return sim::board().digitalRead(pin); }

uint32_t analogReadMilliVolts(uint8_t pin) { return sim::board().adcMilliVolts(pin); }

uint16_t analogRead(uint8_t pin) {
  // 12-bit ADC over a nominal 3.3V range, matching the ESP32-S3 default attenuation.
  const uint32_t mv = sim::board().adcMilliVolts(pin);
  const uint32_t counts = mv * 4095 / 3300;
  return (uint16_t)(counts > 4095 ? 4095 : counts);
}

void analogWrite(uint8_t pin, int value) {
  (void)pin;
  (void)value;
}
void attachInterrupt(uint8_t pin, void (*handler)(void), int mode) {
  (void)pin;
  (void)handler;
  (void)mode;
}
void attachInterruptArg(uint8_t pin, void (*handler)(void*), void* arg, int mode) {
  (void)pin;
  (void)handler;
  (void)arg;
  (void)mode;
}
void detachInterrupt(uint8_t pin) { (void)pin; }
uint8_t digitalPinToInterrupt(uint8_t pin) { return pin; }

// ---------------------------------------------------------------- LEDC / speaker

bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution) {
  (void)pin;
  (void)freq;
  (void)resolution;
  return true;
}
bool ledcAttachChannel(uint8_t pin, uint32_t freq, uint8_t resolution, uint8_t channel) {
  return ledcAttach(pin, freq, resolution);
}
bool ledcDetach(uint8_t pin) {
  sim::board().writeTone(0);
  return true;
}
bool ledcWrite(uint8_t pin, uint32_t duty) {
  // The firmware silences the speaker by writing a zero duty cycle.
  if (duty == 0) sim::board().writeTone(0);
  return true;
}
bool ledcWriteTone(uint8_t pin, uint32_t freq) {
  (void)pin;
  sim::board().writeTone(freq);
  return true;
}
uint32_t ledcReadFreq(uint8_t pin) {
  (void)pin;
  return sim::board().currentTone();
}

// ---------------------------------------------------------------- hardware timers

// hw_timer_t is opaque on device; here the pointer value carries the virtual clock's timer slot.
struct hw_timer_t {
  int handle;
};

namespace {
  hw_timer_t g_timers[8];
}

hw_timer_t* timerBegin(uint32_t frequency) {
  const int handle = sim::clock().addTimer(frequency);
  if (handle < 0) return nullptr;
  g_timers[handle].handle = handle;
  return &g_timers[handle];
}

void timerEnd(hw_timer_t* timer) {
  if (timer) sim::clock().removeTimer(timer->handle);
}

void timerAttachInterrupt(hw_timer_t* timer, void (*handler)(void)) {
  if (timer) sim::clock().setTimerCallback(timer->handle, handler);
}

void timerDetachInterrupt(hw_timer_t* timer) {
  if (timer) sim::clock().setTimerCallback(timer->handle, nullptr);
}

void timerAlarm(hw_timer_t* timer, uint64_t alarmValue, bool autoreload, uint64_t reloadCount) {
  (void)reloadCount;
  if (timer) sim::clock().setTimerAlarm(timer->handle, alarmValue, autoreload);
}

uint64_t timerRead(hw_timer_t* timer) {
  (void)timer;
  return sim::clock().nowUs();
}
void timerWrite(hw_timer_t* timer, uint64_t val) {
  (void)timer;
  (void)val;
}
void timerStart(hw_timer_t* timer) { (void)timer; }
void timerStop(hw_timer_t* timer) { (void)timer; }

// ---------------------------------------------------------------- string conversions

char* itoa(int value, char* buffer, int base) { return ltoa((long)value, buffer, base); }

char* utoa(unsigned int value, char* buffer, int base) {
  if (!buffer) return buffer;
  const String text((unsigned long)value, (unsigned char)base);
  strcpy(buffer, text.c_str());
  return buffer;
}

char* ltoa(long value, char* buffer, int base) {
  if (!buffer) return buffer;
  const String text(value, (unsigned char)base);
  strcpy(buffer, text.c_str());
  return buffer;
}

char* dtostrf(double value, int width, unsigned int precision, char* buffer) {
  if (!buffer) return buffer;
  sprintf(buffer, "%*.*f", width, (int)precision, value);
  return buffer;
}

// ---------------------------------------------------------------- random

namespace {
  std::mt19937& generator() {
    // Fixed seed: two runs of the same scenario should produce the same flight.
    static std::mt19937 gen(0x1EAF5157u);
    return gen;
  }
}  // namespace

long random(long max) { return max <= 0 ? 0 : (long)(generator()() % (uint32_t)max); }
long random(long min, long max) { return max <= min ? min : min + random(max - min); }
void randomSeed(unsigned long seed) { generator().seed((uint32_t)seed); }
extern "C" uint32_t esp_random(void) { return (uint32_t)generator()(); }
