// Arduino core stand-in for the host emulator.
//
// Everything here exists because the firmware calls it, not because Arduino defines it: the goal
// is that src/vario compiles unmodified, and that the calls which mean something on a device
// (pins, time, tone generation) land in the virtual board instead of vanishing.
#pragma once

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "HardwareSerial.h"
#include "Print.h"
#include "Stream.h"
#include "WString.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int word;

// GPIO numbers are a distinct enum in ESP-IDF; the firmware casts pin macros to it when arming
// wake-up sources.  An integer typedef accepts those casts unchanged.
typedef int gpio_num_t;

#define PROGMEM
#define PGM_P const char*
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER 2.718281828459045235360287471352

#define LOW 0x0
#define HIGH 0x1
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
#define OPEN_DRAIN 0x10
#define OUTPUT_OPEN_DRAIN 0x13

#define RISING 0x01
#define FALLING 0x02
#define CHANGE 0x03
#define ONLOW 0x04
#define ONHIGH 0x05

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define _min(a, b) ((a) < (b) ? (a) : (b))
#define _max(a, b) ((a) > (b) ? (a) : (b))
#define sq(x) ((x) * (x))

// Arduino's min/max/abs/constrain are macros on-device.  As templates they keep the same call
// syntax without breaking every standard header the host toolchain pulls in.
template <typename T, typename U>
constexpr auto min(const T& a, const U& b) -> decltype(a < b ? a : b) {
  return a < b ? a : b;
}
template <typename T, typename U>
constexpr auto max(const T& a, const U& b) -> decltype(a > b ? a : b) {
  return a > b ? a : b;
}
template <typename T, typename U, typename V>
constexpr T constrain(T x, U lo, V hi) {
  return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x);
}
constexpr double radians(double degrees) { return degrees * DEG_TO_RAD; }
constexpr double degrees(double radians) { return radians * RAD_TO_DEG; }

inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
  if (inMax == inMin) return outMin;
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// ---------------------------------------------------------------- time
uint32_t millis(void);
uint32_t micros(void);
uint64_t esp_timer_get_time(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield(void);

// ---------------------------------------------------------------- pins
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
uint16_t analogRead(uint8_t pin);
uint32_t analogReadMilliVolts(uint8_t pin);
void analogWrite(uint8_t pin, int value);
void attachInterrupt(uint8_t pin, void (*handler)(void), int mode);
void detachInterrupt(uint8_t pin);
uint8_t digitalPinToInterrupt(uint8_t pin);

// ---------------------------------------------------------------- LEDC (speaker)
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcAttachChannel(uint8_t pin, uint32_t freq, uint8_t resolution, uint8_t channel);
bool ledcDetach(uint8_t pin);
bool ledcWrite(uint8_t pin, uint32_t duty);
bool ledcWriteTone(uint8_t pin, uint32_t freq);
uint32_t ledcReadFreq(uint8_t pin);

// ---------------------------------------------------------------- hardware timers
struct hw_timer_t;
hw_timer_t* timerBegin(uint32_t frequency);
void timerEnd(hw_timer_t* timer);
void timerAttachInterrupt(hw_timer_t* timer, void (*handler)(void));
void timerDetachInterrupt(hw_timer_t* timer);
void timerAlarm(hw_timer_t* timer, uint64_t alarmValue, bool autoreload, uint64_t reloadCount);
uint64_t timerRead(hw_timer_t* timer);
void timerWrite(hw_timer_t* timer, uint64_t val);
void timerStart(hw_timer_t* timer);
void timerStop(hw_timer_t* timer);

// ---------------------------------------------------------------- misc
// itoa/ltoa are non-standard but present in the Arduino toolchain's stdlib.
char* itoa(int value, char* buffer, int base);
char* ltoa(long value, char* buffer, int base);
char* utoa(unsigned int value, char* buffer, int base);
char* dtostrf(double value, int width, unsigned int precision, char* buffer);

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

// The device sizes the Arduino loop task's stack with this; the host thread is already large.
#define SET_LOOP_TASK_STACK_SIZE(sz) int __sim_loop_stack_size = (sz)

#include "Esp.h"
#include "esp_random.h"
#include "esp_rom_uart.h"
#include "esp_system.h"
#include "system/usb_serial.h"
