#pragma once
#include <vapor/config.hpp>

namespace vapor {
// STL-compatible: return a reference to one of the arguments, so do not keep
// the result past the lifetime of the arguments (e.g. when passing temporaries).
template <typename T>
VAPOR_NODISCARD constexpr const T &Min(const T &a, const T &b) {
  return b < a ? b : a;
}
template <typename T, typename Cmp>
VAPOR_NODISCARD constexpr const T &Min(const T &a, const T &b, Cmp cmp) {
  return cmp(b, a) ? b : a;
}
template <typename T>
VAPOR_NODISCARD constexpr const T &Max(const T &a, const T &b) {
  return a < b ? b : a;
}
template <typename T, typename Cmp>
VAPOR_NODISCARD constexpr const T &Max(const T &a, const T &b, Cmp cmp) {
  return cmp(a, b) ? b : a;
}

enum class CommonError {
  OutOfMemory,
  OutOfBounds,
  EmptyString,
  ValueNotFound,
  UnsupportedOperation,
  EmptyPointer,
  EmptyOptional,
  CapacityExceeded,
  InvalidArgument,
  ValueOutOfRange
};

} // namespace vapor
