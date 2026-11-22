#pragma once
#include <vapor/config.hpp>
#include <vapor/utility.hpp>

// Comparison function objects, API-compatible with std::less and friends.
//
// Why: sorting, searching, ordered containers and heaps all take "how do I
// compare two values" as a parameter. These small types are the defaults, and
// give you a named, inlinable alternative to writing a lambda.
//
// How: `Sort(v, Greater<>())` or `PriorityQueue<int, Greater<int>>`.
// `Less<int>` compares two ints. `Less<>` (same as `Less<void>`) is
// "transparent": it compares any two types that support the operator, so it
// avoids a conversion when e.g. looking up a StringView key in a table of
// Strings.

namespace vapor {

#define VAPOR_COMPARATOR(Name, op)                                             \
  template <typename T = void> struct Name {                                   \
    VAPOR_NODISCARD constexpr bool operator()(const T &a, const T &b) const {  \
      return a op b;                                                           \
    }                                                                          \
  };                                                                           \
  template <> struct Name<void> {                                              \
    template <typename A, typename B>                                          \
    VAPOR_NODISCARD constexpr auto operator()(A &&a, B &&b) const              \
        -> decltype(Forward<A>(a) op Forward<B>(b)) {                          \
      return Forward<A>(a) op Forward<B>(b);                                   \
    }                                                                          \
  }

VAPOR_COMPARATOR(Less, <);
VAPOR_COMPARATOR(Greater, >);
VAPOR_COMPARATOR(LessEqual, <=);
VAPOR_COMPARATOR(GreaterEqual, >=);
VAPOR_COMPARATOR(EqualTo, ==);
VAPOR_COMPARATOR(NotEqualTo, !=);

#undef VAPOR_COMPARATOR

} // namespace vapor
