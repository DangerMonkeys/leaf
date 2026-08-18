// Arduino String for the host emulator.
//
// Backed by std::string, with the Arduino API surface the firmware actually uses.  Written from
// the documented behaviour rather than vendored from the ESP32 core, so the emulator stays MIT
// like the rest of this repository.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : v_(s ? s : "") {}
  String(const char* s, size_t len) : v_(s ? std::string(s, len) : std::string()) {}
  String(const String&) = default;
  String(String&&) = default;
  String(const std::string& s) : v_(s) {}
  String(char c) : v_(1, c) {}
  String(unsigned char v, unsigned char base = 10) { v_ = fromInt((long)v, base); }
  String(int v, unsigned char base = 10) { v_ = fromInt((long)v, base); }
  String(unsigned int v, unsigned char base = 10) { v_ = fromUInt((unsigned long)v, base); }
  String(long v, unsigned char base = 10) { v_ = fromInt(v, base); }
  String(unsigned long v, unsigned char base = 10) { v_ = fromUInt(v, base); }
  String(long long v, unsigned char base = 10) { v_ = fromInt((long)v, base); }
  String(unsigned long long v, unsigned char base = 10) { v_ = fromUInt((unsigned long)v, base); }
  String(float v, unsigned int decimalPlaces = 2) { v_ = fromDouble(v, decimalPlaces); }
  String(double v, unsigned int decimalPlaces = 2) { v_ = fromDouble(v, decimalPlaces); }

  String& operator=(const String&) = default;
  String& operator=(String&&) = default;
  String& operator=(const char* s) {
    v_ = s ? s : "";
    return *this;
  }

  // ---------------------------------------------------------------- inspection
  const char* c_str() const { return v_.c_str(); }
  size_t length() const { return v_.size(); }
  bool isEmpty() const { return v_.empty(); }
  char charAt(size_t i) const { return i < v_.size() ? v_[i] : '\0'; }
  char operator[](size_t i) const { return charAt(i); }
  char& operator[](size_t i) { return v_[i]; }
  void setCharAt(size_t i, char c) {
    if (i < v_.size()) v_[i] = c;
  }
  const std::string& str() const { return v_; }

  // Arduino's String is iterable, and libraries (IgcLogger among them) rely on it.
  char* begin() { return v_.empty() ? nullptr : &v_[0]; }
  char* end() { return begin() + v_.size(); }
  const char* begin() const { return v_.data(); }
  const char* end() const { return v_.data() + v_.size(); }
  explicit operator bool() const { return !v_.empty(); }

  // ---------------------------------------------------------------- comparison
  bool equals(const String& o) const { return v_ == o.v_; }
  bool equals(const char* s) const { return s && v_ == s; }
  bool equalsIgnoreCase(const String& o) const {
    if (v_.size() != o.v_.size()) return false;
    for (size_t i = 0; i < v_.size(); i++) {
      if (tolower((unsigned char)v_[i]) != tolower((unsigned char)o.v_[i])) return false;
    }
    return true;
  }
  int compareTo(const String& o) const { return v_.compare(o.v_); }
  bool startsWith(const String& p) const { return v_.rfind(p.v_, 0) == 0; }
  bool endsWith(const String& s) const {
    return v_.size() >= s.v_.size() && v_.compare(v_.size() - s.v_.size(), s.v_.size(), s.v_) == 0;
  }

  // ---------------------------------------------------------------- search / slice
  int indexOf(char c, size_t from = 0) const { return topos(v_.find(c, from)); }
  int indexOf(const String& s, size_t from = 0) const { return topos(v_.find(s.v_, from)); }
  int lastIndexOf(char c) const { return topos(v_.rfind(c)); }
  int lastIndexOf(const String& s) const { return topos(v_.rfind(s.v_)); }
  String substring(size_t from) const {
    return from >= v_.size() ? String() : String(v_.substr(from));
  }
  String substring(size_t from, size_t to) const {
    if (from >= v_.size() || to <= from) return String();
    return String(v_.substr(from, to - from));
  }

  // ---------------------------------------------------------------- mutation
  void reserve(size_t n) { v_.reserve(n); }
  void clear() { v_.clear(); }
  void remove(size_t index) {
    if (index < v_.size()) v_.erase(index);
  }
  void remove(size_t index, size_t count) {
    if (index < v_.size()) v_.erase(index, count);
  }
  void trim() {
    const size_t first = v_.find_first_not_of(" \t\r\n\v\f");
    if (first == std::string::npos) {
      v_.clear();
      return;
    }
    v_ = v_.substr(first, v_.find_last_not_of(" \t\r\n\v\f") - first + 1);
  }
  void toUpperCase() {
    std::transform(v_.begin(), v_.end(), v_.begin(), [](unsigned char c) { return toupper(c); });
  }
  void toLowerCase() {
    std::transform(v_.begin(), v_.end(), v_.begin(), [](unsigned char c) { return tolower(c); });
  }
  void replace(char from, char to) { std::replace(v_.begin(), v_.end(), from, to); }
  void replace(const String& from, const String& to) {
    if (from.v_.empty()) return;
    size_t pos = 0;
    while ((pos = v_.find(from.v_, pos)) != std::string::npos) {
      v_.replace(pos, from.v_.size(), to.v_);
      pos += to.v_.size();
    }
  }
  bool concat(const String& o) {
    v_ += o.v_;
    return true;
  }
  bool concat(const char* s) {
    if (s) v_ += s;
    return true;
  }
  bool concat(char c) {
    v_ += c;
    return true;
  }
  void getBytes(unsigned char* buf, size_t bufsize) const {
    if (!buf || bufsize == 0) return;
    const size_t n = std::min(bufsize - 1, v_.size());
    memcpy(buf, v_.data(), n);
    buf[n] = 0;
  }
  void toCharArray(char* buf, size_t bufsize) const { getBytes((unsigned char*)buf, bufsize); }

  // ---------------------------------------------------------------- conversion
  long toInt() const { return strtol(v_.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(v_.c_str(), nullptr); }
  double toDouble() const { return strtod(v_.c_str(), nullptr); }

  // ---------------------------------------------------------------- operators
  template <typename T>
  String& operator+=(const T& value) {
    v_ += String(value).v_;
    return *this;
  }
  String& operator+=(const String& o) {
    v_ += o.v_;
    return *this;
  }
  String& operator+=(const char* s) {
    if (s) v_ += s;
    return *this;
  }
  String& operator+=(char c) {
    v_ += c;
    return *this;
  }

  friend String operator+(const String& a, const String& b) { return String(a.v_ + b.v_); }
  friend bool operator==(const String& a, const String& b) { return a.v_ == b.v_; }
  friend bool operator!=(const String& a, const String& b) { return a.v_ != b.v_; }
  friend bool operator<(const String& a, const String& b) { return a.v_ < b.v_; }
  friend bool operator>(const String& a, const String& b) { return a.v_ > b.v_; }
  friend bool operator<=(const String& a, const String& b) { return a.v_ <= b.v_; }
  friend bool operator>=(const String& a, const String& b) { return a.v_ >= b.v_; }

 private:
  static int topos(size_t p) { return p == std::string::npos ? -1 : (int)p; }

  static std::string fromInt(long v, unsigned char base) {
    if (base == 10) return std::to_string(v);
    return fromUInt((unsigned long)v, base);
  }
  static std::string fromUInt(unsigned long v, unsigned char base) {
    if (base == 10) return std::to_string(v);
    static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (base < 2 || base > 36) base = 10;
    std::string out;
    do {
      out.insert(out.begin(), digits[v % base]);
      v /= base;
    } while (v);
    return out;
  }
  static std::string fromDouble(double v, unsigned int places) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", (int)places, v);
    return buf;
  }

  std::string v_;
};

// Arduino's F() macro is a no-op off-device; the type it yields still has to exist.
class __FlashStringHelper;
#define F(string_literal) (string_literal)
#define PSTR(s) (s)
