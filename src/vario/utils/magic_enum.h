#pragma once

#include <Arduino.h>  // for String
#include <magic_enum.hpp>
#include <type_traits>

// Constrain to enum class (scoped enum): it's an enum and not implicitly convertible to int
template <typename TEnum,
          typename = std::enable_if_t<std::is_enum_v<TEnum> && !std::is_convertible_v<TEnum, int>>>
inline String nameOf(TEnum value) {
  auto sv = magic_enum::enum_name(value);  // std::string_view
  return String(sv.data(), sv.size());     // construct Arduino String from ptr+len
}
