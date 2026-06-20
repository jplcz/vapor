#pragma once
#include <stddef.h>
#include <vapor/config.hpp>

#if defined(VAPOR_USE_STL)
#include <type_traits>
#endif

namespace vapor {

// The base template defaults to false.
template <typename T> struct IsCloneable {
  typedef T value_type;
  static const bool value = false;
};

#if defined(VAPOR_USE_CONCEPTS)
template <typename T>
concept TriviallyRelocatable = std::is_trivially_copyable_v<T>;

template <typename T> struct IsRelocatable {
  static const bool value = TriviallyRelocatable<T>;
  typedef T value_type;
};
#elif defined(VAPOR_USE_TYPE_TRAITS)
template <typename T> struct IsRelocatable {
  static const bool value = std::is_trivially_copyable<T>::value;
  typedef T value_type;
};
#else
// Core Freestanding Baseline: Default to false, manually opt-in.
// Explicitly specialize for basic types so primitives are fast out of the box.
template <typename T> struct IsRelocatable {
  static const bool value = false;
  typedef T value_type;
};
#endif

// Wire up basic primitives to be trivially relocatable by default
template <typename T> struct IsRelocatable<T *> {
  static const bool value = true;
  typedef T *value_type;
};
template <> struct IsRelocatable<char> {
  static const bool value = true;
  typedef char value_type;
};
template <> struct IsRelocatable<int> {
  static const bool value = true;
  typedef int value_type;
};
template <> struct IsRelocatable<size_t> {
  static const bool value = true;
  typedef size_t value_type;
};
template <> struct IsRelocatable<float> {
  static const bool value = true;
  typedef float value_type;
};

} // namespace vapor
