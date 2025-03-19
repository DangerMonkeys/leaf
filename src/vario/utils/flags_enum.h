#pragma once

#include <iostream>
#include <type_traits>

// Base class template for flag enums
template <typename Enum>
class FlagsEnum {
 public:
  using UnderlyingType = typename std::underlying_type<Enum>::type;

  constexpr FlagsEnum() : value(0) {}
  constexpr FlagsEnum(Enum e) : value(static_cast<UnderlyingType>(e)) {}
  constexpr FlagsEnum(UnderlyingType v) : value(v) {}

  // Bitwise OR
  constexpr FlagsEnum operator|(FlagsEnum other) const { return FlagsEnum(value | other.value); }

  // Bitwise AND
  constexpr FlagsEnum operator&(FlagsEnum other) const { return FlagsEnum(value & other.value); }

  // Bitwise OR assignment
  FlagsEnum& operator|=(FlagsEnum other) {
    value |= other.value;
    return *this;
  }

  // Bitwise AND assignment
  FlagsEnum& operator&=(FlagsEnum other) {
    value &= other.value;
    return *this;
  }

  // Check if a flag is set
  constexpr bool hasFlag(Enum flag) const {
    return (value & static_cast<UnderlyingType>(flag)) == static_cast<UnderlyingType>(flag);
  }

  // Implicit conversion back to Enum type
  constexpr operator Enum() const { return static_cast<Enum>(value); }

  // Get underlying value
  constexpr UnderlyingType raw() const { return value; }

 private:
  UnderlyingType value;
};

#define DEFINE_FLAGS_ENUM(EnumName, UnderlyingType)                  \
  enum class EnumName : UnderlyingType;                              \
  inline FlagsEnum<EnumName> operator|(EnumName lhs, EnumName rhs) { \
    return FlagsEnum<EnumName>(lhs) | FlagsEnum<EnumName>(rhs);      \
  }                                                                  \
  inline FlagsEnum<EnumName> operator&(EnumName lhs, EnumName rhs) { \
    return FlagsEnum<EnumName>(lhs) & FlagsEnum<EnumName>(rhs);      \
  }                                                                  \
  enum class EnumName : UnderlyingType

#define FLAG_SET(value, flag) (((value) & (flag)) == (flag))
