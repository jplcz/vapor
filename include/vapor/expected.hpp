#pragma once
#include <assert.h>
#include <vapor/config.hpp>
#include <vapor/detail/placement_new.hpp>
#include <vapor/utility.hpp>

#if defined(VAPOR_USE_STL) && (__cplusplus >= 202002L)
#if defined(__has_include)
#if __has_include(<expected>)
#include <expected>
#endif
#endif
#endif

#if defined(VAPOR_USE_STL) && defined(__cpp_lib_expected)
#define VAPOR_HAS_STD_EXPECTED 1
#endif

namespace vapor {
template <typename E> struct VAPOR_NODISCARD_TYPE Unexpected {
  E error;

  VAPOR_CXX14_CONSTEXPR explicit Unexpected(E err) : error(Move(err)) {}
};

template <typename E>
VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR Unexpected<E> MakeUnexpected(E err) {
  return Unexpected<E>(Move(err));
}

template <typename T, typename E> class VAPOR_NODISCARD_TYPE Expected {
private:
  union {
    T m_value;
    E m_error;
  };

  bool m_has_value;

  // Internal helper to clear current state cleanly
  void destroy() noexcept {
    if (m_has_value) {
      m_value.~T();
    } else {
      m_error.~E();
    }
  }

public:
  // --- Standard Constructors ---
  VAPOR_CXX14_CONSTEXPR Expected() : m_has_value(true) { new (&m_value) T(); }

  VAPOR_CXX14_CONSTEXPR Expected(const T &val) : m_has_value(true) {
    new (&m_value) T(val);
  }

  VAPOR_CXX14_CONSTEXPR Expected(T &&val) : m_has_value(true) {
    new (&m_value) T(Move(val));
  }

  VAPOR_CXX14_CONSTEXPR Expected(const Unexpected<E> &unex)
      : m_has_value(false) {
    new (&m_error) E(unex.error);
  }

  VAPOR_CXX14_CONSTEXPR Expected(Unexpected<E> &&unex) : m_has_value(false) {
    new (&m_error) E(Move(unex.error));
  }

  // --- Copy & Move Rules (Required for Non-POD Union types) ---
  VAPOR_CXX14_CONSTEXPR Expected(const Expected &other)
      : m_has_value(other.m_has_value) {
    if (m_has_value) {
      new (&m_value) T(other.m_value);
    } else {
      new (&m_error) E(other.m_error);
    }
  }

  VAPOR_CXX14_CONSTEXPR Expected(Expected &&other) noexcept
      : m_has_value(other.m_has_value) {
    if (m_has_value) {
      new (&m_value) T(Move(other.m_value));
    } else {
      new (&m_error) E(Move(other.m_error));
    }
  }

  Expected &operator=(const Expected &other) {
    if (this != &other) {
      destroy();
      m_has_value = other.m_has_value;
      if (m_has_value) {
        new (&m_value) T(other.m_value);
      } else {
        new (&m_error) E(other.m_error);
      }
    }
    return *this;
  }

  Expected &operator=(Expected &&other) noexcept {
    if (this != &other) {
      destroy();
      m_has_value = other.m_has_value;
      if (m_has_value) {
        new (&m_value) T(Move(other.m_value));
      } else {
        new (&m_error) E(Move(other.m_error));
      }
    }
    return *this;
  }

  ~Expected() { destroy(); }

  // --- std::expected Interoperability ---
#if defined(VAPOR_HAS_STD_EXPECTED)
  // Construct from a std::expected
  VAPOR_CXX14_CONSTEXPR Expected(const std::expected<T, E> &other)
      : m_has_value(other.has_value()) {
    if (m_has_value) {
      new (&m_value) T(other.value());
    } else {
      new (&m_error) E(other.error());
    }
  }

  VAPOR_CXX14_CONSTEXPR Expected(std::expected<T, E> &&other)
      : m_has_value(other.has_value()) {
    if (m_has_value) {
      new (&m_value) T(Move(other.value()));
    } else {
      new (&m_error) E(Move(other.error()));
    }
  }

  // Implicit conversion operator to std::expected
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR operator std::expected<T, E>() const & {
    if (m_has_value) {
      return m_value;
    }
    return std::unexpected<E>(m_error);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR operator std::expected<T, E>() && {
    if (m_has_value) {
      return Move(m_value);
    }
    return std::unexpected<E>(Move(m_error));
  }
#endif

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool has_value() const noexcept {
    return m_has_value;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR explicit
  operator bool() const noexcept {
    return m_has_value;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const T &value() const & {
    assert(m_has_value);
    return m_value;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T &&value() && {
    assert(m_has_value);
    return Move(m_value);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T &value() & {
    assert(m_has_value);
    return m_value;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const T &&value() const && {
    assert(m_has_value);
    return static_cast<const T &&>(m_value);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const T *operator->() const noexcept {
    assert(m_has_value);
    return &m_value;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T *operator->() noexcept {
    assert(m_has_value);
    return &m_value;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const T &operator*() const & noexcept {
    assert(m_has_value);
    return m_value;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T &operator*() & noexcept {
    assert(m_has_value);
    return m_value;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T &&operator*() && noexcept {
    assert(m_has_value);
    return Move(m_value);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const E &error() const & {
    assert(!m_has_value);
    return m_error;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR E &error() & {
    assert(!m_has_value);
    return m_error;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR E &&error() && {
    assert(!m_has_value);
    return Move(m_error);
  }

  template <typename U>
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T value_or(U &&default_value) const & {
    return m_has_value ? m_value : static_cast<T>(Forward<U>(default_value));
  }

  // Moves the value out of a temporary instead of copying it
  template <typename U>
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T value_or(U &&default_value) && {
    return m_has_value ? Move(m_value)
                       : static_cast<T>(Forward<U>(default_value));
  }
};

template <typename E> class VAPOR_NODISCARD_TYPE Expected<void, E> {
private:
  struct empty {};

  union {
    empty m_no_value;
    E m_error;
  };
  bool m_has_value;

  void destroy() noexcept {
    if (!m_has_value) {
      m_error.~E();
    }
  }

public:
  // Default constructor indicates a generic "success" state
  VAPOR_CXX14_CONSTEXPR Expected() noexcept : m_has_value(true) {
    new (&m_no_value) empty();
  }

  VAPOR_CXX14_CONSTEXPR Expected(const Unexpected<E> &unex)
      : m_has_value(false) {
    new (&m_error) E(unex.error);
  }

  VAPOR_CXX14_CONSTEXPR Expected(Unexpected<E> &&unex) : m_has_value(false) {
    new (&m_error) E(Move(unex.error));
  }

  // --- Copy & Move Lifecycles ---

  VAPOR_CXX14_CONSTEXPR Expected(const Expected &other)
      : m_has_value(other.m_has_value) {
    if (!m_has_value) {
      new (&m_error) E(other.m_error);
    }
  }

  VAPOR_CXX14_CONSTEXPR Expected(Expected &&other) noexcept
      : m_has_value(other.m_has_value) {
    if (!m_has_value) {
      new (&m_error) E(Move(other.m_error));
    }
  }

  Expected &operator=(const Expected &other) {
    if (this != &other) {
      destroy();
      m_has_value = other.m_has_value;
      if (!m_has_value) {
        new (&m_error) E(other.m_error);
      }
    }
    return *this;
  }

  Expected &operator=(Expected &&other) noexcept {
    if (this != &other) {
      destroy();
      m_has_value = other.m_has_value;
      if (!m_has_value) {
        new (&m_error) E(Move(other.m_error));
      }
    }
    return *this;
  }

  VAPOR_CXX20_CONSTEXPR ~Expected() { destroy(); }

  // --- std::expected<void, E> Interoperability ---
#if defined(VAPOR_HAS_STD_EXPECTED)
  VAPOR_CXX14_CONSTEXPR Expected(const std::expected<void, E> &other)
      : m_has_value(other.has_value()) {
    if (!m_has_value) {
      new (&m_error) E(other.error());
    }
  }

  VAPOR_CXX14_CONSTEXPR Expected(std::expected<void, E> &&other)
      : m_has_value(other.has_value()) {
    if (!m_has_value) {
      new (&m_error) E(Move(other.error()));
    }
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR
  operator std::expected<void, E>() const & {
    if (m_has_value)
      return {};
    return std::unexpected<E>(m_error);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR operator std::expected<void, E>() && {
    if (m_has_value)
      return {};
    return std::unexpected<E>(Move(m_error));
  }
#endif

  // --- Accessors ---

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool has_value() const noexcept {
    return m_has_value;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR explicit
  operator bool() const noexcept {
    return m_has_value;
  }

  // value() returns void, primarily checking assertions
  VAPOR_CXX14_CONSTEXPR void value() const { assert(m_has_value); }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const E &error() const & {
    assert(!m_has_value);
    return m_error;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR E &error() & {
    assert(!m_has_value);
    return m_error;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR E &&error() && {
    assert(!m_has_value);
    return Move(m_error);
  }
};

} // namespace vapor
