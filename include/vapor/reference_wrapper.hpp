#pragma once
#include "config.hpp"

namespace vapor {

template <typename T> class ReferenceWrapper {
private:
  T *m_ptr;

public:
  // Construct from a raw reference
  VAPOR_CXX14_CONSTEXPR ReferenceWrapper(T &ref) noexcept : m_ptr(&ref) {}

  // Implicit conversion back to a raw reference
  VAPOR_CXX14_CONSTEXPR operator T &() const noexcept { return *m_ptr; }

  // Explicitly fetch the underlying reference
  VAPOR_CXX14_CONSTEXPR T &get() const noexcept { return *m_ptr; }

  // Arrow operator to transparently access members if T is a struct/class
  VAPOR_CXX14_CONSTEXPR T *operator->() const noexcept { return m_ptr; }
};

} // namespace vapor
