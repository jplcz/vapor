#pragma once
#include <vapor/config.hpp>

namespace vapor {
template <typename T> VAPOR_NODISCARD inline T Min(T a, T b) noexcept {
  return (a < b) ? a : b;
}

template <typename T> VAPOR_NODISCARD inline T Max(T a, T b) noexcept {
  return (a > b) ? a : b;
}
} // namespace vapor
