// Arduino Print / Printable for the host emulator.
#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print;

class Printable {
 public:
  virtual ~Printable() = default;
  virtual size_t printTo(Print& p) const = 0;
};

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; i++) n += write(buffer[i]);
    return n;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* buffer, size_t size) { return write((const uint8_t*)buffer, size); }
  virtual int availableForWrite() { return 128; }
  virtual void flush() {}

  size_t print(const char* s) { return write(s); }
  size_t print(const String& s) { return write(s.c_str()); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char v, int base = DEC) { return print(String((unsigned long)v, base)); }
  size_t print(int v, int base = DEC) { return print(String((long)v, base)); }
  size_t print(unsigned int v, int base = DEC) { return print(String((unsigned long)v, base)); }
  size_t print(long v, int base = DEC) { return print(String(v, base)); }
  size_t print(unsigned long v, int base = DEC) { return print(String(v, base)); }
  size_t print(long long v, int base = DEC) { return print(String((long)v, base)); }
  size_t print(unsigned long long v, int base = DEC) {
    return print(String((unsigned long)v, base));
  }
  size_t print(double v, int places = 2) { return print(String(v, (unsigned int)places)); }
  size_t print(const Printable& p) { return p.printTo(*this); }

  size_t println() { return write("\r\n"); }
  template <typename T>
  size_t println(T value) {
    return print(value) + println();
  }
  template <typename T>
  size_t println(T value, int base) {
    return print(value, base) + println();
  }

  size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    va_list args;
    va_start(args, format);
    char stack[256];
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(stack, sizeof(stack), format, copy);
    va_end(copy);
    size_t written = 0;
    if (needed < 0) {
      va_end(args);
      return 0;
    }
    if ((size_t)needed < sizeof(stack)) {
      written = write((const uint8_t*)stack, (size_t)needed);
    } else {
      char* heap = (char*)malloc((size_t)needed + 1);
      if (heap) {
        vsnprintf(heap, (size_t)needed + 1, format, args);
        written = write((const uint8_t*)heap, (size_t)needed);
        free(heap);
      }
    }
    va_end(args);
    return written;
  }
};
