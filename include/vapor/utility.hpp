#pragma once
#include <vapor/config.hpp>
#include <vapor/type_traits.hpp>

// STL-free replacements for <utility> helpers.
//
// Why: moving a value or perfectly forwarding a template argument needs a
// cast to an rvalue reference. Writing static_cast<T &&>(x) everywhere is
// noisy and easy to get wrong; named helpers state the intent.
//
// How: Move(x) always yields an rvalue (you give up x). Forward<T>(x) keeps
// the value category the caller used (lvalue stays lvalue, rvalue stays
// rvalue) and must be called with the deduced template type, e.g.
// `template <typename T> void f(T &&x) { g(Forward<T>(x)); }`.

namespace vapor {

template <typename T>
VAPOR_NODISCARD constexpr RemoveReferenceT<T> &&Move(T &&value) noexcept {
  return static_cast<RemoveReferenceT<T> &&>(value);
}

template <typename T>
VAPOR_NODISCARD constexpr T &&Forward(RemoveReferenceT<T> &value) noexcept {
  return static_cast<T &&>(value);
}

template <typename T>
VAPOR_NODISCARD constexpr T &&Forward(RemoveReferenceT<T> &&value) noexcept {
  static_assert(!IsLvalueReference<T>::value,
                "Forward of an rvalue as an lvalue reference is ill-formed");
  return static_cast<T &&>(value);
}

template <typename T>
VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR T *AddressOf(T &value) noexcept {
  return reinterpret_cast<T *>(
      &const_cast<char &>(reinterpret_cast<const volatile char &>(value)));
}

template <typename T> VAPOR_CXX14_CONSTEXPR void Swap(T &a, T &b) noexcept {
  T tmp(Move(a));
  a = Move(b);
  b = Move(tmp);
}

template <typename T, typename U = T>
VAPOR_CXX14_CONSTEXPR T Exchange(T &obj, U &&new_value) {
  T old(Move(obj));
  obj = Forward<U>(new_value);
  return old;
}

// A compile-time list of indices, used to expand a parameter pack alongside
// the elements of a Tuple. MakeIndexSequence<3> is IndexSequence<0, 1, 2>.
template <size_t... Is> struct IndexSequence {
  typedef IndexSequence type;
  static constexpr size_t size() noexcept { return sizeof...(Is); }
};

namespace detail {
template <size_t N, size_t... Is>
struct MakeIndexSequenceImpl : MakeIndexSequenceImpl<N - 1, N - 1, Is...> {};
template <size_t... Is> struct MakeIndexSequenceImpl<0, Is...> {
  typedef IndexSequence<Is...> type;
};
} // namespace detail

template <size_t N>
using MakeIndexSequence = typename detail::MakeIndexSequenceImpl<N>::type;

} // namespace vapor
