#pragma once
#include <cstdint>
#include <type_traits>

// -------------------------
// Constraint behavior tags
struct wrap_t {};
struct clamp_t {};

// -------------------------
// Policy: specialize per enum
template <typename E>
struct enum_policy;

// -------------------------
// Detection: enable operators only if a policy exists
namespace detail {
  template <typename, typename = void>
  struct has_policy : std::false_type {};

  template <typename E>
  struct has_policy<E, std::void_t<decltype(enum_policy<E>::bottom), decltype(enum_policy<E>::top),
                                   typename enum_policy<E>::behavior>> : std::true_type {};
}  // namespace detail

// -------------------------
// Helpers implementing ++/--
namespace detail {

  template <typename E>
  inline E& inc(E& e, wrap_t) {
    using U = std::underlying_type_t<E>;
    const U v = static_cast<U>(e);
    const U vb = static_cast<U>(enum_policy<E>::bottom);
    const U vt = static_cast<U>(enum_policy<E>::top);
    e = static_cast<E>((v >= vt) ? vb : (v + 1));
    return e;
  }

  template <typename E>
  inline E& inc(E& e, clamp_t) {
    using U = std::underlying_type_t<E>;
    const U v = static_cast<U>(e);
    const U vt = static_cast<U>(enum_policy<E>::top);
    e = static_cast<E>((v >= vt) ? vt : (v + 1));
    return e;
  }

  template <typename E>
  inline E& dec(E& e, wrap_t) {
    using U = std::underlying_type_t<E>;
    const U v = static_cast<U>(e);
    const U vb = static_cast<U>(enum_policy<E>::bottom);
    const U vt = static_cast<U>(enum_policy<E>::top);
    e = static_cast<E>((v <= vb) ? vt : (v - 1));
    return e;
  }

  template <typename E>
  inline E& dec(E& e, clamp_t) {
    using U = std::underlying_type_t<E>;
    const U v = static_cast<U>(e);
    const U vb = static_cast<U>(enum_policy<E>::bottom);
    e = static_cast<E>((v <= vb) ? vb : (v - 1));
    return e;
  }

}  // namespace detail

// -------------------------
// Generic operators: enabled only when enum_policy<E> is valid
template <typename E,
          typename std::enable_if_t<std::is_enum_v<E> && detail::has_policy<E>::value, int> = 0>
inline E& operator++(E& e) {
  return detail::inc(e, typename enum_policy<E>::behavior{});
}

template <typename E,
          typename std::enable_if_t<std::is_enum_v<E> && detail::has_policy<E>::value, int> = 0>
inline E operator++(E& e, int) {
  E tmp = e;
  ++e;
  return tmp;
}

template <typename E,
          typename std::enable_if_t<std::is_enum_v<E> && detail::has_policy<E>::value, int> = 0>
inline E& operator--(E& e) {
  return detail::dec(e, typename enum_policy<E>::behavior{});
}

template <typename E,
          typename std::enable_if_t<std::is_enum_v<E> && detail::has_policy<E>::value, int> = 0>
inline E operator--(E& e, int) {
  E tmp = e;
  --e;
  return tmp;
}

#define DEFINE_WRAPPING_BOUNDS(EnumType, Bottom, Top) \
  template <>                                         \
  struct enum_policy<EnumType> {                      \
    using behavior = wrap_t;                          \
    static constexpr EnumType bottom = Bottom;        \
    static constexpr EnumType top = Top;              \
  }

#define DEFINE_CLAMPING_BOUNDS(EnumType, Bottom, Top) \
  template <>                                         \
  struct enum_policy<EnumType> {                      \
    using behavior = clamp_t;                         \
    static constexpr EnumType bottom = Bottom;        \
    static constexpr EnumType top = Top;              \
  }
