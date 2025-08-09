#pragma once

#include <cstdint>
#include <type_traits>

template <typename E>
struct clamped_bounds;  // to be specialized per-enum

template <typename E, typename U = std::underlying_type_t<E>,
          std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E& operator++(E& e) {
  constexpr E bot = clamped_bounds<E>::bottom;
  constexpr E top = clamped_bounds<E>::top;
  U v = static_cast<U>(e);
  U vb = static_cast<U>(bot);
  U vt = static_cast<U>(top);
  e = static_cast<E>((v >= vt) ? vt : (v + 1));
  return e;
}

template <typename E, typename U = std::underlying_type_t<E>,
          std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E operator++(E& e, int) {
  E tmp = e;
  ++e;
  return tmp;
}

template <typename E, typename U = std::underlying_type_t<E>,
          std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E& operator--(E& e) {
  constexpr E bot = clamped_bounds<E>::bottom;
  constexpr E top = clamped_bounds<E>::top;
  U v = static_cast<U>(e);
  U vb = static_cast<U>(bot);
  U vt = static_cast<U>(top);
  e = static_cast<E>((v <= vb) ? vb : (v - 1));
  return e;
}

template <typename E, typename U = std::underlying_type_t<E>,
          std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E operator--(E& e, int) {
  E tmp = e;
  --e;
  return tmp;
}

#define DEFINE_CLAMPED_BOUNDS(EnumType, Bottom, Top) \
  template <>                                        \
  struct clamped_bounds<EnumType> {                  \
    static constexpr EnumType bottom = Bottom;       \
    static constexpr EnumType top = Top;             \
  }
