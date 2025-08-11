#pragma once

#include <Preferences.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>

/// @brief Individual setting, regardless of data type.
/// @tparam T Underlying data type of setting.
/// @tparam DefaultValue Default value of setting.
template <typename T, T DefaultValue>
class Setting {
 public:
  using ChangeHandler = std::function<void(const T&)>;

  // Constructor that captures a string literal or char array and enforces length at compile time
  template <std::size_t N>
  explicit Setting(const char (&key)[N]) : key_(key), value_(DefaultValue) {
    static_assert(N > 0, "Key must not be empty");
    static_assert(N - 1 <= 15, "Key must be at most 15 characters long");
  }

  // Fallback constructor for runtime-provided keys (kept for flexibility).
  // We add a lightweight runtime check.
  explicit Setting(const char* key) : key_(key), value_(DefaultValue) {
    assert(key_ != nullptr && "Key pointer must not be null");
    // Runtime guard (cheap). In production, replace with your own error handling if desired.
    if (key_ && std::strlen(key_) > 15) {
      // You can replace with fatalError("Preferences key too long") if you prefer.
      assert(false && "Key must be at most 15 characters long");
    }
  }

  static constexpr T defaultValue() { return DefaultValue; }

  const char* key() const { return key_; }

  // Set the value back to DefaultValue and fire onChange. Uses virtual operator=.
  void loadDefault() { this->operator=(DefaultValue); }

  // Virtual so derived classes can clamp/validate before storing
  virtual Setting& operator=(const T& newValue) {
    value_ = newValue;
    if (onChangeHandler_) onChangeHandler_(value_);
    return *this;
  }

  // Implicit conversion to the underlying type
  operator T() const { return value_; }

  void onChange(ChangeHandler handler) { onChangeHandler_ = std::move(handler); }

  // Arduino Preferences is not const-correct; keep non-const refs
  virtual void readFrom(Preferences& prefs) = 0;
  virtual void putInto(Preferences& prefs) const = 0;

 protected:
  const char* const key_;
  T value_;
  ChangeHandler onChangeHandler_ = nullptr;
};

/// @brief Individual numeric setting with clamped assignment and ++/-- within bounds.
/// @tparam T Underlying data type of setting.
/// @tparam MinValue Minimum value setting may take on.
/// @tparam DefaultValue Default value of setting.
/// @tparam MaxValue Maximum value setting may take on.
template <typename T, T MinValue, T DefaultValue, T MaxValue,
          typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
class NumericSetting : public Setting<T, DefaultValue> {
  using Base = Setting<T, DefaultValue>;

 public:
  using Base::Base;  // inherit constructors (both compile-time and runtime key forms)

  static constexpr T min() { return MinValue; }
  static constexpr T max() { return MaxValue; }

  static_assert(MinValue <= MaxValue, "NumericSetting: MinValue must be <= MaxValue");
  static_assert(DefaultValue >= MinValue, "NumericSetting: DefaultValue must be >= MinValue");
  static_assert(DefaultValue <= MaxValue, "NumericSetting: DefaultValue must be <= MaxValue");

  // Override the base assignment to enforce bounds
  Base& operator=(const T& newValue) override {
    if (newValue >= MinValue && newValue <= MaxValue) {
      return Base::operator=(newValue);
    }
    // Ignore out-of-range writes; keep existing value
    return *this;
  }

  // Increment/decrement operators (prefix/postfix)
  NumericSetting& operator++() {
    if (this->value_ < MaxValue) Base::operator=(static_cast<T>(this->value_ + 1));
    return *this;
  }
  NumericSetting operator++(int) {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }

  NumericSetting& operator--() {
    if (this->value_ > MinValue) Base::operator=(static_cast<T>(this->value_ - 1));
    return *this;
  }
  NumericSetting operator--(int) {
    auto tmp = *this;
    --(*this);
    return tmp;
  }
};

/// @brief Individual int8_t setting (stored as Preferences char).
/// @tparam MinValue Minimum value setting may take on.
/// @tparam DefaultValue Default value of setting.
/// @tparam MaxValue Maximum value setting may take on.
template <int8_t MinValue, int8_t DefaultValue, int8_t MaxValue>
class CharSetting : public NumericSetting<int8_t, MinValue, DefaultValue, MaxValue> {
  using Base = NumericSetting<int8_t, MinValue, DefaultValue, MaxValue>;

 public:
  using Base::Base;       // inherit constructor(s)
  using Base::operator=;  // make Base::operator=(int8_t) visible

  void readFrom(Preferences& prefs) override {
    auto v = prefs.getChar(this->key(), Base::defaultValue());
    Base::operator=(v);  // clamps/validates
  }

  void putInto(Preferences& prefs) const override {
    prefs.putChar(this->key(), static_cast<char>(this->value_));
  }
};
