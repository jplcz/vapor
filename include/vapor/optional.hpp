#pragma once
#include <assert.h>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>

#if defined(VAPOR_USE_STL) && (__cplusplus >= 201703L)
#include <optional>
#define VAPOR_HAS_STD_OPTIONAL 1
#endif

namespace vapor {

// Tag meaning "no value", e.g. `Optional<int> o = NullOpt;`
struct NullOptT {
  explicit constexpr NullOptT(int) noexcept {}
};
constexpr NullOptT NullOpt{0};

// A value that may or may not be present. Unlike Expected it carries no error
// code; use it when "absent" is a normal answer (e.g. a lookup that finds
// nothing). Never allocates.
template <typename T> class VAPOR_NODISCARD Optional {
  static_assert(!IsReference<T>::value,
                "Optional<T&> is not supported; use ReferenceWrapper<T>");

  union {
    char m_empty;
    T m_value;
  };
  bool m_engaged;

  void destroy() noexcept {
    if (m_engaged) {
      m_value.~T();
      m_engaged = false;
    }
  }

public:
  using value_type = T;

  Optional() noexcept : m_empty(0), m_engaged(false) {}
  Optional(NullOptT) noexcept : m_empty(0), m_engaged(false) {}

  Optional(const T &v) : m_engaged(true) { ::new (&m_value) T(v); }
  Optional(T &&v) : m_engaged(true) { ::new (&m_value) T(Move(v)); }

  Optional(const Optional &other) : m_empty(0), m_engaged(false) {
    if (other.m_engaged) {
      ::new (&m_value) T(other.m_value);
      m_engaged = true;
    }
  }

  Optional(Optional &&other) noexcept : m_empty(0), m_engaged(false) {
    if (other.m_engaged) {
      ::new (&m_value) T(Move(other.m_value));
      m_engaged = true;
    }
  }

  ~Optional() { destroy(); }

  Optional &operator=(NullOptT) noexcept {
    destroy();
    return *this;
  }

  Optional &operator=(const Optional &other) {
    if (this == &other)
      return *this;
    if (m_engaged && other.m_engaged) {
      m_value = other.m_value;
    } else if (other.m_engaged) {
      ::new (&m_value) T(other.m_value);
      m_engaged = true;
    } else {
      destroy();
    }
    return *this;
  }

  Optional &operator=(Optional &&other) noexcept {
    if (this == &other)
      return *this;
    if (m_engaged && other.m_engaged) {
      m_value = Move(other.m_value);
    } else if (other.m_engaged) {
      ::new (&m_value) T(Move(other.m_value));
      m_engaged = true;
    } else {
      destroy();
    }
    return *this;
  }

  // --- State ---
  VAPOR_NODISCARD bool has_value() const noexcept { return m_engaged; }
  VAPOR_NODISCARD explicit operator bool() const noexcept { return m_engaged; }

  // --- Access (STL names). std::optional throws on an empty value(); without
  // exceptions these assert instead, and checked_value() is the non-trapping
  // alternative.
  VAPOR_NODISCARD T &operator*() & noexcept {
    assert(m_engaged);
    return m_value;
  }
  VAPOR_NODISCARD const T &operator*() const & noexcept {
    assert(m_engaged);
    return m_value;
  }
  VAPOR_NODISCARD T &&operator*() && noexcept {
    assert(m_engaged);
    return Move(m_value);
  }

  VAPOR_NODISCARD T *operator->() noexcept {
    assert(m_engaged);
    return &m_value;
  }
  VAPOR_NODISCARD const T *operator->() const noexcept {
    assert(m_engaged);
    return &m_value;
  }

  VAPOR_NODISCARD T &value() & noexcept { return **this; }
  VAPOR_NODISCARD const T &value() const & noexcept { return **this; }
  VAPOR_NODISCARD T &&value() && noexcept { return Move(**this); }

  VAPOR_NODISCARD Expected<ReferenceWrapper<T>, CommonError>
  checked_value() noexcept {
    if (!m_engaged)
      return MakeUnexpected(CommonError::EmptyOptional);
    return ReferenceWrapper<T>(m_value);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const T>, CommonError>
  checked_value() const noexcept {
    if (!m_engaged)
      return MakeUnexpected(CommonError::EmptyOptional);
    return ReferenceWrapper<const T>(m_value);
  }

  template <typename U> VAPOR_NODISCARD T value_or(U &&fallback) const & {
    return m_engaged ? m_value : static_cast<T>(Forward<U>(fallback));
  }
  template <typename U> VAPOR_NODISCARD T value_or(U &&fallback) && {
    return m_engaged ? Move(m_value) : static_cast<T>(Forward<U>(fallback));
  }

  // --- Modifiers ---
  template <typename... Args> T &emplace(Args &&...args) {
    destroy();
    ::new (&m_value) T(Forward<Args>(args)...);
    m_engaged = true;
    return m_value;
  }

  void reset() noexcept { destroy(); }

  void swap(Optional &other) {
    Optional tmp(Move(other));
    other = Move(*this);
    *this = Move(tmp);
  }

#if defined(VAPOR_HAS_STD_OPTIONAL)
  Optional(const std::optional<T> &o) : m_empty(0), m_engaged(false) {
    if (o) {
      ::new (&m_value) T(*o);
      m_engaged = true;
    }
  }
  VAPOR_NODISCARD operator std::optional<T>() const & {
    if (m_engaged)
      return std::optional<T>(m_value);
    return std::nullopt;
  }
  VAPOR_NODISCARD operator std::optional<T>() && {
    if (m_engaged)
      return std::optional<T>(Move(m_value));
    return std::nullopt;
  }
#endif
};

template <typename T>
VAPOR_NODISCARD bool operator==(const Optional<T> &a,
                                const Optional<T> &b) {
  if (a.has_value() != b.has_value())
    return false;
  return !a.has_value() || *a == *b;
}
template <typename T>
VAPOR_NODISCARD bool operator!=(const Optional<T> &a,
                                const Optional<T> &b) {
  return !(a == b);
}
template <typename T>
VAPOR_NODISCARD bool operator==(const Optional<T> &a, NullOptT) noexcept {
  return !a.has_value();
}
template <typename T>
VAPOR_NODISCARD bool operator==(NullOptT, const Optional<T> &a) noexcept {
  return !a.has_value();
}
template <typename T>
VAPOR_NODISCARD bool operator!=(const Optional<T> &a, NullOptT) noexcept {
  return a.has_value();
}
template <typename T>
VAPOR_NODISCARD bool operator!=(NullOptT, const Optional<T> &a) noexcept {
  return a.has_value();
}
template <typename T>
VAPOR_NODISCARD bool operator==(const Optional<T> &a, const T &v) {
  return a.has_value() && *a == v;
}
template <typename T>
VAPOR_NODISCARD bool operator!=(const Optional<T> &a, const T &v) {
  return !(a == v);
}

#if __cplusplus >= 201703L
template <typename T> Optional(T) -> Optional<T>;
#endif

} // namespace vapor
