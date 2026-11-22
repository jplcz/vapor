#pragma once
#include <stddef.h>
#include <vapor/config.hpp>
#include <vapor/pair.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// Tuple<Ts...>: a fixed-size heterogeneous list, API-compatible with
// std::tuple.
//
// Why: Pair holds exactly two values; Tuple generalises that to any number,
// e.g. for returning several results or bundling arguments.
//
// How: `Tuple<int, float, bool> t(1, 2.0f, true); Get<1>(t);`. `Tie(a, b) =
// MakePair(1, 2)` assigns through references. Empty element types take no
// space (empty base optimisation).

namespace vapor {

template <typename... Ts> class Tuple;

namespace detail {

struct TupleAccess;

struct TupleTag {};

template <bool... Bs> struct BoolPack {};
template <bool... Bs>
struct AllTrue : IsSame<BoolPack<true, Bs...>, BoolPack<Bs..., true>> {};

template <typename S, typename... Us> struct IsSelfArg : FalseType {};
template <typename S, typename U>
struct IsSelfArg<S, U> : IsSame<DecayT<U>, S> {};

template <size_t I, typename... Ts> struct TypeAt;
template <typename H, typename... Ts> struct TypeAt<0, H, Ts...> {
  typedef H type;
};
template <size_t I, typename H, typename... Ts> struct TypeAt<I, H, Ts...> {
  typedef typename TypeAt<I - 1, Ts...>::type type;
};

template <typename T, typename... Ts> struct CountType;
template <typename T> struct CountType<T> : IntegralConstant<size_t, 0> {};
template <typename T, typename H, typename... Ts>
struct CountType<T, H, Ts...>
    : IntegralConstant<size_t, (IsSame<T, H>::value ? 1 : 0) +
                                   CountType<T, Ts...>::value> {};

template <size_t I, typename T, typename... Ts> struct IndexOfImpl;
template <size_t I, typename T, typename H, typename... Ts>
struct IndexOfImpl<I, T, H, Ts...>
    : Conditional<IsSame<T, H>::value, IntegralConstant<size_t, I>,
                  IndexOfImpl<I + 1, T, Ts...>>::type {};

// One tuple element. Empty, non-final classes are inherited from instead of
// stored so they occupy no space.
template <size_t I, typename T,
          bool UseBase = IsEmpty<T>::value && !IsFinal<T>::value>
struct TupleLeaf {
  T value;
  constexpr TupleLeaf() : value() {}
  template <typename U>
  constexpr TupleLeaf(TupleTag, U &&u) : value(Forward<U>(u)) {}
};
template <size_t I, typename T> struct TupleLeaf<I, T, true> : T {
  constexpr TupleLeaf() : T() {}
  template <typename U>
  constexpr TupleLeaf(TupleTag, U &&u) : T(Forward<U>(u)) {}
};

template <typename Seq, typename... Ts> struct TupleImpl;
template <size_t... Is, typename... Ts>
struct TupleImpl<IndexSequence<Is...>, Ts...> : TupleLeaf<Is, Ts>... {
  constexpr TupleImpl() : TupleLeaf<Is, Ts>()... {}
  template <typename... Us>
  constexpr TupleImpl(TupleTag t, Us &&...us)
      : TupleLeaf<Is, Ts>(t, Forward<Us>(us))... {}
};

template <size_t I, typename T, bool B>
constexpr T &LeafRef(TupleLeaf<I, T, B> &l, FalseType) noexcept {
  return l.value;
}
template <size_t I, typename T, bool B>
constexpr const T &LeafRef(const TupleLeaf<I, T, B> &l, FalseType) noexcept {
  return l.value;
}
template <size_t I, typename T, bool B>
constexpr T &LeafRef(TupleLeaf<I, T, B> &l, TrueType) noexcept {
  return static_cast<T &>(l);
}
template <size_t I, typename T, bool B>
constexpr const T &LeafRef(const TupleLeaf<I, T, B> &l, TrueType) noexcept {
  return static_cast<const T &>(l);
}

} // namespace detail

template <typename... Ts>
struct TupleSize<Tuple<Ts...>> : IntegralConstant<size_t, sizeof...(Ts)> {};
template <size_t I, typename... Ts> struct TupleElement<I, Tuple<Ts...>> {
  static_assert(I < sizeof...(Ts), "Tuple index out of range");
  typedef typename detail::TypeAt<I, Ts...>::type type;
};

// Get<I>(tuple): element access by position.
template <size_t I, typename... Ts>
VAPOR_NODISCARD constexpr TupleElementT<I, Tuple<Ts...>> &
Get(Tuple<Ts...> &t) noexcept;
template <size_t I, typename... Ts>
VAPOR_NODISCARD constexpr const TupleElementT<I, Tuple<Ts...>> &
Get(const Tuple<Ts...> &t) noexcept;
template <size_t I, typename... Ts>
VAPOR_NODISCARD constexpr TupleElementT<I, Tuple<Ts...>> &&
Get(Tuple<Ts...> &&t) noexcept;

template <typename... Ts> class Tuple {
  typedef detail::TupleImpl<MakeIndexSequence<sizeof...(Ts)>, Ts...> Impl;
  Impl m_impl;

  template <typename Tp, size_t... Is>
  Tuple(detail::TupleTag t, Tp &&other, IndexSequence<Is...>)
      : m_impl(t, Get<Is>(Forward<Tp>(other))...) {}

  template <typename Tp, size_t... Is>
  void AssignFrom(Tp &&other, IndexSequence<Is...>) {
    int dummy[] = {0, ((void)(Get<Is>(*this) = Get<Is>(Forward<Tp>(other))),
                       0)...};
    (void)dummy;
  }
  template <size_t... Is> void SwapWith(Tuple &o, IndexSequence<Is...>) {
    int dummy[] = {0, ((void)Swap(Get<Is>(*this), Get<Is>(o)), 0)...};
    (void)dummy;
  }

  template <typename... Us> friend class Tuple;
  friend struct detail::TupleAccess;

public:
  constexpr Tuple() : m_impl() {}

  constexpr Tuple(const Ts &...args)
      : m_impl(detail::TupleTag(), args...) {}

  template <typename... Us,
            EnableIfT<sizeof...(Us) == sizeof...(Ts) &&
                          detail::AllTrue<IsConvertible<Us &&, Ts>::value...>::value &&
                          !detail::IsSelfArg<Tuple, Us...>::value,
                      int> = 0>
  constexpr Tuple(Us &&...args)
      : m_impl(detail::TupleTag(), Forward<Us>(args)...) {}

  Tuple(const Tuple &) = default;
  Tuple(Tuple &&) = default;

  template <typename... Us,
            EnableIfT<sizeof...(Us) == sizeof...(Ts) &&
                          detail::AllTrue<IsConvertible<const Us &, Ts>::value...>::value,
                      int> = 0>
  Tuple(const Tuple<Us...> &other)
      : Tuple(detail::TupleTag(), other, MakeIndexSequence<sizeof...(Ts)>()) {}

  template <typename... Us,
            EnableIfT<sizeof...(Us) == sizeof...(Ts) &&
                          detail::AllTrue<IsConvertible<Us &&, Ts>::value...>::value,
                      int> = 0>
  Tuple(Tuple<Us...> &&other)
      : Tuple(detail::TupleTag(), Move(other),
              MakeIndexSequence<sizeof...(Ts)>()) {}

  Tuple &operator=(const Tuple &other) {
    AssignFrom(other, MakeIndexSequence<sizeof...(Ts)>());
    return *this;
  }
  Tuple &operator=(Tuple &&other) {
    AssignFrom(Move(other), MakeIndexSequence<sizeof...(Ts)>());
    return *this;
  }
  template <typename... Us,
            EnableIfT<sizeof...(Us) == sizeof...(Ts), int> = 0>
  Tuple &operator=(const Tuple<Us...> &other) {
    AssignFrom(other, MakeIndexSequence<sizeof...(Ts)>());
    return *this;
  }
  template <typename... Us,
            EnableIfT<sizeof...(Us) == sizeof...(Ts), int> = 0>
  Tuple &operator=(Tuple<Us...> &&other) {
    AssignFrom(Move(other), MakeIndexSequence<sizeof...(Ts)>());
    return *this;
  }

  void swap(Tuple &other) {
    SwapWith(other, MakeIndexSequence<sizeof...(Ts)>());
  }
};

template <> class Tuple<> {
public:
  constexpr Tuple() noexcept {}
  void swap(Tuple &) noexcept {}
};

namespace detail {
// Grants Get() access to Tuple's private storage.
struct TupleAccess {
  template <typename... Ts>
  static constexpr typename Tuple<Ts...>::Impl &impl(Tuple<Ts...> &t) noexcept {
    return t.m_impl;
  }
  template <typename... Ts>
  static constexpr const typename Tuple<Ts...>::Impl &
  impl(const Tuple<Ts...> &t) noexcept {
    return t.m_impl;
  }
};
} // namespace detail

template <size_t I, typename... Ts>
VAPOR_NODISCARD constexpr TupleElementT<I, Tuple<Ts...>> &
Get(Tuple<Ts...> &t) noexcept {
  typedef typename detail::TypeAt<I, Ts...>::type T;
  return detail::LeafRef<I>(
      static_cast<detail::TupleLeaf<I, T> &>(detail::TupleAccess::impl(t)),
      IntegralConstant<bool, IsEmpty<T>::value && !IsFinal<T>::value>());
}
template <size_t I, typename... Ts>
VAPOR_NODISCARD constexpr const TupleElementT<I, Tuple<Ts...>> &
Get(const Tuple<Ts...> &t) noexcept {
  typedef typename detail::TypeAt<I, Ts...>::type T;
  return detail::LeafRef<I>(
      static_cast<const detail::TupleLeaf<I, T> &>(
          detail::TupleAccess::impl(t)),
      IntegralConstant<bool, IsEmpty<T>::value && !IsFinal<T>::value>());
}
template <size_t I, typename... Ts>
VAPOR_NODISCARD constexpr TupleElementT<I, Tuple<Ts...>> &&
Get(Tuple<Ts...> &&t) noexcept {
  return static_cast<TupleElementT<I, Tuple<Ts...>> &&>(Get<I>(t));
}

// Get<T>(tuple): element access by type; T must occur exactly once.
template <typename T, typename... Ts>
VAPOR_NODISCARD constexpr T &Get(Tuple<Ts...> &t) noexcept {
  static_assert(detail::CountType<T, Ts...>::value == 1,
                "type must occur exactly once in the Tuple");
  return Get<detail::IndexOfImpl<0, T, Ts...>::value>(t);
}
template <typename T, typename... Ts>
VAPOR_NODISCARD constexpr const T &Get(const Tuple<Ts...> &t) noexcept {
  static_assert(detail::CountType<T, Ts...>::value == 1,
                "type must occur exactly once in the Tuple");
  return Get<detail::IndexOfImpl<0, T, Ts...>::value>(t);
}
template <typename T, typename... Ts>
VAPOR_NODISCARD constexpr T &&Get(Tuple<Ts...> &&t) noexcept {
  static_assert(detail::CountType<T, Ts...>::value == 1,
                "type must occur exactly once in the Tuple");
  return Get<detail::IndexOfImpl<0, T, Ts...>::value>(Move(t));
}

template <typename... Ts>
VAPOR_NODISCARD constexpr Tuple<DecayT<Ts>...> MakeTuple(Ts &&...args) {
  return Tuple<DecayT<Ts>...>(Forward<Ts>(args)...);
}

// Tuple of references; assigning to it assigns through to the originals.
template <typename... Ts>
VAPOR_NODISCARD constexpr Tuple<Ts &...> Tie(Ts &...args) noexcept {
  return Tuple<Ts &...>(args...);
}

template <typename... Ts>
VAPOR_NODISCARD constexpr Tuple<Ts &&...> ForwardAsTuple(Ts &&...args) noexcept {
  return Tuple<Ts &&...>(Forward<Ts>(args)...);
}

template <typename... Ts> void Swap(Tuple<Ts...> &a, Tuple<Ts...> &b) {
  a.swap(b);
}

namespace detail {
template <size_t I, size_t N> struct TupleCompare {
  template <typename A, typename B>
  static bool eq(const A &a, const B &b) {
    return Get<I>(a) == Get<I>(b) && TupleCompare<I + 1, N>::eq(a, b);
  }
  template <typename A, typename B>
  static bool lt(const A &a, const B &b) {
    return Get<I>(a) < Get<I>(b)
               ? true
               : (Get<I>(b) < Get<I>(a) ? false
                                        : TupleCompare<I + 1, N>::lt(a, b));
  }
};
template <size_t N> struct TupleCompare<N, N> {
  template <typename A, typename B> static bool eq(const A &, const B &) {
    return true;
  }
  template <typename A, typename B> static bool lt(const A &, const B &) {
    return false;
  }
};
} // namespace detail

template <typename... As, typename... Bs>
VAPOR_NODISCARD bool operator==(const Tuple<As...> &a, const Tuple<Bs...> &b) {
  static_assert(sizeof...(As) == sizeof...(Bs), "Tuple sizes must match");
  return detail::TupleCompare<0, sizeof...(As)>::eq(a, b);
}
template <typename... As, typename... Bs>
VAPOR_NODISCARD bool operator!=(const Tuple<As...> &a, const Tuple<Bs...> &b) {
  return !(a == b);
}
template <typename... As, typename... Bs>
VAPOR_NODISCARD bool operator<(const Tuple<As...> &a, const Tuple<Bs...> &b) {
  static_assert(sizeof...(As) == sizeof...(Bs), "Tuple sizes must match");
  return detail::TupleCompare<0, sizeof...(As)>::lt(a, b);
}
template <typename... As, typename... Bs>
VAPOR_NODISCARD bool operator>(const Tuple<As...> &a, const Tuple<Bs...> &b) {
  return b < a;
}
template <typename... As, typename... Bs>
VAPOR_NODISCARD bool operator<=(const Tuple<As...> &a, const Tuple<Bs...> &b) {
  return !(b < a);
}
template <typename... As, typename... Bs>
VAPOR_NODISCARD bool operator>=(const Tuple<As...> &a, const Tuple<Bs...> &b) {
  return !(a < b);
}

namespace detail {
template <typename F, typename Tp, size_t... Is>
auto ApplyImpl(F &&f, Tp &&t, IndexSequence<Is...>)
    -> decltype(Forward<F>(f)(Get<Is>(Forward<Tp>(t))...)) {
  return Forward<F>(f)(Get<Is>(Forward<Tp>(t))...);
}
} // namespace detail

// Apply(f, tuple): calls f with the tuple's elements as arguments.
template <typename F, typename Tp>
auto Apply(F &&f, Tp &&t) -> decltype(detail::ApplyImpl(
    Forward<F>(f), Forward<Tp>(t),
    MakeIndexSequence<TupleSize<RemoveCVT<RemoveReferenceT<Tp>>>::value>())) {
  return detail::ApplyImpl(
      Forward<F>(f), Forward<Tp>(t),
      MakeIndexSequence<TupleSize<RemoveCVT<RemoveReferenceT<Tp>>>::value>());
}

} // namespace vapor
