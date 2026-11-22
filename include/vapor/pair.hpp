#pragma once
#include <stddef.h>
#include <vapor/config.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// Pair<A, B>: two values bundled together, API-compatible with std::pair.
//
// Why: functions that need to hand back two things (a key and a value, an
// iterator and a flag) should not have to invent a struct each time.
//
// How: `Pair<int, float> p(1, 2.0f); p.first; p.second;` or
// `auto p = MakePair(1, 2.0f);`. `Get<0>(p)` / `Get<1>(p)` give generic access,
// which is what Tuple and structured bindings use.

namespace vapor {

// Number of elements and the type of element I of a Pair or Tuple.
template <typename T> struct TupleSize;
template <size_t I, typename T> struct TupleElement;

template <typename T>
struct TupleSize<const T> : IntegralConstant<size_t, TupleSize<T>::value> {};
template <size_t I, typename T> struct TupleElement<I, const T> {
  typedef const typename TupleElement<I, T>::type type;
};
template <size_t I, typename T>
using TupleElementT = typename TupleElement<I, T>::type;

template <typename A, typename B> struct Pair {
  typedef A first_type;
  typedef B second_type;

  A first;
  B second;

  constexpr Pair() : first(), second() {}
  constexpr Pair(const A &a, const B &b) : first(a), second(b) {}

  template <typename U1, typename U2,
            EnableIfT<IsConvertible<U1 &&, A>::value &&
                          IsConvertible<U2 &&, B>::value,
                      int> = 0>
  constexpr Pair(U1 &&a, U2 &&b)
      : first(Forward<U1>(a)), second(Forward<U2>(b)) {}

  template <typename U1, typename U2,
            EnableIfT<IsConvertible<const U1 &, A>::value &&
                          IsConvertible<const U2 &, B>::value,
                      int> = 0>
  constexpr Pair(const Pair<U1, U2> &o) : first(o.first), second(o.second) {}

  template <typename U1, typename U2,
            EnableIfT<IsConvertible<U1 &&, A>::value &&
                          IsConvertible<U2 &&, B>::value,
                      int> = 0>
  constexpr Pair(Pair<U1, U2> &&o)
      : first(Forward<U1>(o.first)), second(Forward<U2>(o.second)) {}

  Pair(const Pair &) = default;
  Pair(Pair &&) = default;
  Pair &operator=(const Pair &) = default;
  Pair &operator=(Pair &&) = default;

  template <typename U1, typename U2> Pair &operator=(const Pair<U1, U2> &o) {
    first = o.first;
    second = o.second;
    return *this;
  }
  template <typename U1, typename U2> Pair &operator=(Pair<U1, U2> &&o) {
    first = Forward<U1>(o.first);
    second = Forward<U2>(o.second);
    return *this;
  }

  void swap(Pair &o) {
    Swap(first, o.first);
    Swap(second, o.second);
  }
};

template <typename A, typename B>
struct TupleSize<Pair<A, B>> : IntegralConstant<size_t, 2> {};
template <typename A, typename B> struct TupleElement<0, Pair<A, B>> {
  typedef A type;
};
template <typename A, typename B> struct TupleElement<1, Pair<A, B>> {
  typedef B type;
};

template <typename A, typename B>
VAPOR_NODISCARD constexpr Pair<DecayT<A>, DecayT<B>> MakePair(A &&a, B &&b) {
  return Pair<DecayT<A>, DecayT<B>>(Forward<A>(a), Forward<B>(b));
}

template <typename A, typename B>
void Swap(Pair<A, B> &a, Pair<A, B> &b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

template <typename A, typename B, typename C, typename D>
VAPOR_NODISCARD constexpr bool operator==(const Pair<A, B> &x,
                                          const Pair<C, D> &y) {
  return x.first == y.first && x.second == y.second;
}
template <typename A, typename B, typename C, typename D>
VAPOR_NODISCARD constexpr bool operator!=(const Pair<A, B> &x,
                                          const Pair<C, D> &y) {
  return !(x == y);
}
template <typename A, typename B, typename C, typename D>
VAPOR_NODISCARD constexpr bool operator<(const Pair<A, B> &x,
                                         const Pair<C, D> &y) {
  return x.first < y.first || (!(y.first < x.first) && x.second < y.second);
}
template <typename A, typename B, typename C, typename D>
VAPOR_NODISCARD constexpr bool operator>(const Pair<A, B> &x,
                                         const Pair<C, D> &y) {
  return y < x;
}
template <typename A, typename B, typename C, typename D>
VAPOR_NODISCARD constexpr bool operator<=(const Pair<A, B> &x,
                                          const Pair<C, D> &y) {
  return !(y < x);
}
template <typename A, typename B, typename C, typename D>
VAPOR_NODISCARD constexpr bool operator>=(const Pair<A, B> &x,
                                          const Pair<C, D> &y) {
  return !(x < y);
}

namespace detail {
template <size_t I> struct PairGet;
template <> struct PairGet<0> {
  template <typename P>
  static constexpr auto get(P &p) noexcept -> decltype((p.first)) {
    return p.first;
  }
};
template <> struct PairGet<1> {
  template <typename P>
  static constexpr auto get(P &p) noexcept -> decltype((p.second)) {
    return p.second;
  }
};
} // namespace detail

// Get<I>(pair): element access by position (0 or 1).
template <size_t I, typename A, typename B>
VAPOR_NODISCARD constexpr TupleElementT<I, Pair<A, B>> &
Get(Pair<A, B> &p) noexcept {
  return detail::PairGet<I>::get(p);
}
template <size_t I, typename A, typename B>
VAPOR_NODISCARD constexpr const TupleElementT<I, Pair<A, B>> &
Get(const Pair<A, B> &p) noexcept {
  return detail::PairGet<I>::get(p);
}
template <size_t I, typename A, typename B>
VAPOR_NODISCARD constexpr TupleElementT<I, Pair<A, B>> &&
Get(Pair<A, B> &&p) noexcept {
  return static_cast<TupleElementT<I, Pair<A, B>> &&>(
      detail::PairGet<I>::get(p));
}

} // namespace vapor

// Structured bindings (`auto [k, v] = pair;`) work out of the box because the
// members are public. Tuple-style access (`Get<I>`) is what Tuple builds on.
