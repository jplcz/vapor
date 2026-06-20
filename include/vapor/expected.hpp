#pragma once
#include <assert.h>
#include <vapor/config.hpp>

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
template <typename E> struct VAPOR_NODISCARD Unexpected {
  E error;

  VAPOR_CXX14_CONSTEXPR explicit Unexpected(E err) : error(err) {}
};

template <typename E>
VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR Unexpected<E> MakeUnexpected(E err) {
  return Unexpected<E>(err);
}

template <typename T, typename E> class VAPOR_NODISCARD Expected {
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
  VAPOR_CXX14_CONSTEXPR Expected(const T &val) : m_has_value(true) {
    new (&m_value) T(val);
  }

  VAPOR_CXX14_CONSTEXPR Expected(T &&val) : m_has_value(true) {
    new (&m_value) T(static_cast<T &&>(val));
  }

  VAPOR_CXX14_CONSTEXPR Expected(const Unexpected<E> &unex)
      : m_has_value(false) {
    new (&m_error) E(unex.error);
  }

  VAPOR_CXX14_CONSTEXPR Expected(Unexpected<E> &&unex) : m_has_value(false) {
    new (&m_error) E(static_cast<E &&>(unex.error));
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
      new (&m_value) T((other.m_value));
    } else {
      new (&m_error) E((other.m_error));
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
        new (&m_value) T((other.m_value));
      } else {
        new (&m_error) E((other.m_error));
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
      new (&m_value) T((other.value()));
    } else {
      new (&m_error) E((other.error()));
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
      return (m_value);
    }
    return std::unexpected<E>((m_error));
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

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T &value() & {
    assert(m_has_value);
    return m_value;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const T *operator->() const noexcept {
    return &m_value;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T *operator->() noexcept {
    return &m_value;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const T &operator*() const & noexcept {
    return m_value;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T &operator*() & noexcept {
    return m_value;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const E &error() const & {
    assert(!m_has_value);
    return m_error;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR E &error() & {
    assert(!m_has_value);
    return m_error;
  }

  template <typename U>
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T value_or(U &&default_value) const & {
    return m_has_value ? m_value : static_cast<T>((default_value));
  }
};
} // namespace vapor
