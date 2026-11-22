#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <vapor/config.hpp>
#include <vapor/detail/placement_new.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// Variant<A, B, C>: a value that is exactly one of several types (a "tagged
// union"), with the type checked by the compiler.
//
// Why: protocols, parsers and state machines often hold "an int, or a string,
// or nothing". A raw `union` forgets which member is alive and leaks or
// corrupts non-trivial types. Variant remembers the active alternative,
// constructs and destroys it correctly, and never allocates.
//
// How:
//   Variant<int, float> v(3);                  // holds the int 3
//   v = 2.5f;                                  // now holds the float
//   if (const float *f = GetIf<float>(&v)) {}  // pointer, or nullptr if not
//   int n = Get<int>(v);                       // aborts if it is not an int
//   Visit(visitor, v);                         // call visitor with the value
//
// Differences from std::variant (which relies on exceptions): a Variant is
// never "valueless". It starts holding a default-constructed first
// alternative (use Monostate first when that type has no default constructor),
// and Get<>() on the wrong alternative aborts instead of throwing. The
// alternatives' copy/move operations must not throw; Variant is copyable or
// movable only when used that way with copyable/movable alternatives.
// Alternatives must be distinct, non-reference, non-array, non-void types;
// at most 254 of them.

namespace vapor {

// An empty alternative: use it as the first type to make a Variant "empty".
struct Monostate {};
inline bool operator==(Monostate, Monostate) { return true; }
inline bool operator!=(Monostate, Monostate) { return false; }
inline bool operator<(Monostate, Monostate) { return false; }

// Tags to construct/emplace a specific alternative.
template <typename T> struct InPlaceTypeTag {};
template <size_t I> struct InPlaceIndexTag {};
#if __cplusplus >= 201402L
template <typename T> constexpr InPlaceTypeTag<T> InPlaceType{};
template <size_t I> constexpr InPlaceIndexTag<I> InPlaceIndex{};
#endif

template <typename... Ts> class Variant;

namespace detail {

template <size_t I, typename... Ts> struct TypeAtImpl;
template <typename T, typename... Ts> struct TypeAtImpl<0, T, Ts...> {
  typedef T type;
};
template <size_t I, typename T, typename... Ts>
struct TypeAtImpl<I, T, Ts...> : TypeAtImpl<I - 1, Ts...> {};

// Index of T among Ts, or Count when absent; Matches counts duplicates.
template <typename T, size_t I, typename... Ts> struct FindType {
  static constexpr size_t index = I;
  static constexpr size_t matches = 0;
};
template <typename T, size_t I, typename U, typename... Ts>
struct FindType<T, I, U, Ts...> {
  typedef FindType<T, I + 1, Ts...> Rest;
  static constexpr bool same = IsSame<T, U>::value;
  static constexpr size_t index = same ? I : Rest::index;
  static constexpr size_t matches = Rest::matches + (same ? 1 : 0);
};

constexpr size_t MaxOf(size_t a, size_t b) { return a > b ? a : b; }
template <typename... Ts> struct MaxSize;
template <> struct MaxSize<> {
  static constexpr size_t size = 1;
  static constexpr size_t align = 1;
};
template <typename T, typename... Ts> struct MaxSize<T, Ts...> {
  static constexpr size_t size = MaxOf(sizeof(T), MaxSize<Ts...>::size);
  static constexpr size_t align = MaxOf(alignof(T), MaxSize<Ts...>::align);
};

// Overload set used to pick the alternative a value converts to: the best
// matching f(T_i) wins, and the result type carries i.
template <size_t I, typename... Ts> struct VariantOverloads {
  static void f();
};
template <size_t I, typename T, typename... Ts>
struct VariantOverloads<I, T, Ts...> : VariantOverloads<I + 1, Ts...> {
  using VariantOverloads<I + 1, Ts...>::f;
  static IntegralConstant<size_t, I> f(T);
};
template <typename U, typename... Ts>
using VariantPick = decltype(VariantOverloads<0, Ts...>::f(Declval<U>()));

template <typename U, typename Variant, typename = void>
struct CanConvertToVariant : FalseType {};
template <typename U, typename... Ts>
struct CanConvertToVariant<
    U, Variant<Ts...>,
    typename EnableIf<(sizeof(VariantPick<U, Ts...>) > 0)>::type> : TrueType {};

} // namespace detail

template <size_t I, typename V> struct VariantAlternative;
template <size_t I, typename... Ts>
struct VariantAlternative<I, Variant<Ts...>> : detail::TypeAtImpl<I, Ts...> {};
template <size_t I, typename V>
using VariantAlternativeT = typename VariantAlternative<I, V>::type;

template <typename V> struct VariantSize;
template <typename... Ts>
struct VariantSize<Variant<Ts...>> : IntegralConstant<size_t, sizeof...(Ts)> {};

template <typename... Ts> class VAPOR_NODISCARD_TYPE Variant {
  static_assert(sizeof...(Ts) > 0, "Variant needs at least one alternative");
  static_assert(sizeof...(Ts) < 255, "Variant supports at most 254 types");
  typedef typename detail::TypeAtImpl<0, Ts...>::type First;

public:
  Variant() noexcept(IsNothrowConstructible<First>::value) : index_(0) {
    new (storage_) First();
  }

  Variant(const Variant &o) : index_(o.index_) { CopyFrom(o); }
  Variant(Variant &&o) noexcept : index_(o.index_) { MoveFrom(o); }

  // Converting constructor: Variant<int, String> v("x") etc.
  template <
      typename U, typename D = DecayT<U>,
      typename = EnableIfT<!IsSame<D, Variant>::value &&
                           detail::CanConvertToVariant<U, Variant>::value>>
  Variant(U &&value) {
    constexpr size_t I = decltype(detail::VariantPick<U, Ts...>())::value;
    typedef typename detail::TypeAtImpl<I, Ts...>::type T;
    new (storage_) T(Forward<U>(value));
    index_ = static_cast<unsigned char>(I);
  }

  template <typename T, typename... Args,
            typename = EnableIfT<detail::FindType<T, 0, Ts...>::matches == 1>>
  explicit Variant(InPlaceTypeTag<T>, Args &&...args) {
    new (storage_) T(Forward<Args>(args)...);
    index_ = static_cast<unsigned char>(detail::FindType<T, 0, Ts...>::index);
  }

  template <size_t I, typename... Args,
            typename = EnableIfT<(I < sizeof...(Ts))>>
  explicit Variant(InPlaceIndexTag<I>, Args &&...args) {
    typedef typename detail::TypeAtImpl<I, Ts...>::type T;
    new (storage_) T(Forward<Args>(args)...);
    index_ = static_cast<unsigned char>(I);
  }

  ~Variant() { Destroy(); }

  Variant &operator=(const Variant &o) {
    if (this == &o)
      return *this;
    if (index_ == o.index_) {
      static void (*const table[])(void *, const void *) = {&AssignCopy<Ts>...};
      table[index_](storage_, o.storage_);
    } else {
      Destroy();
      index_ = o.index_;
      CopyFrom(o);
    }
    return *this;
  }

  Variant &operator=(Variant &&o) noexcept {
    if (this == &o)
      return *this;
    if (index_ == o.index_) {
      static void (*const table[])(void *, void *) = {&AssignMove<Ts>...};
      table[index_](storage_, o.storage_);
    } else {
      Destroy();
      index_ = o.index_;
      MoveFrom(o);
    }
    return *this;
  }

  template <
      typename U, typename D = DecayT<U>,
      typename = EnableIfT<!IsSame<D, Variant>::value &&
                           detail::CanConvertToVariant<U, Variant>::value>>
  Variant &operator=(U &&value) {
    constexpr size_t I = decltype(detail::VariantPick<U, Ts...>())::value;
    typedef typename detail::TypeAtImpl<I, Ts...>::type T;
    if (index_ == I) {
      *static_cast<T *>(Raw()) = Forward<U>(value);
    } else {
      Destroy();
      new (storage_) T(Forward<U>(value));
      index_ = static_cast<unsigned char>(I);
    }
    return *this;
  }

  VAPOR_NODISCARD size_t index() const noexcept { return index_; }

  // Destroys the current value and constructs alternative I in place.
  template <size_t I, typename... Args>
  typename detail::TypeAtImpl<I, Ts...>::type &emplace(Args &&...args) {
    static_assert(I < sizeof...(Ts), "alternative index out of range");
    typedef typename detail::TypeAtImpl<I, Ts...>::type T;
    Destroy();
    T *p = new (storage_) T(Forward<Args>(args)...);
    index_ = static_cast<unsigned char>(I);
    return *p;
  }
  template <typename T, typename... Args> T &emplace(Args &&...args) {
    static_assert(detail::FindType<T, 0, Ts...>::matches == 1,
                  "type must appear exactly once");
    return emplace<detail::FindType<T, 0, Ts...>::index>(
        Forward<Args>(args)...);
  }

  void swap(Variant &o) {
    if (this == &o)
      return;
    Variant tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }

  // Unchecked access to the storage; prefer Get/GetIf/Visit.
  VAPOR_NODISCARD void *Raw() noexcept { return storage_; }
  VAPOR_NODISCARD const void *Raw() const noexcept { return storage_; }

private:
  template <typename T> static void AssignCopy(void *dst, const void *src) {
    *static_cast<T *>(dst) = *static_cast<const T *>(src);
  }
  template <typename T> static void AssignMove(void *dst, void *src) {
    *static_cast<T *>(dst) = Move(*static_cast<T *>(src));
  }
  template <typename T> static void DestroyOne(void *p) {
    static_cast<T *>(p)->~T();
  }
  template <typename T> static void CopyOne(void *dst, const void *src) {
    new (dst) T(*static_cast<const T *>(src));
  }
  template <typename T> static void MoveOne(void *dst, void *src) {
    new (dst) T(Move(*static_cast<T *>(src)));
  }

  void Destroy() noexcept {
    static void (*const table[])(void *) = {&DestroyOne<Ts>...};
    table[index_](storage_);
  }
  void CopyFrom(const Variant &o) {
    static void (*const table[])(void *, const void *) = {&CopyOne<Ts>...};
    table[o.index_](storage_, o.storage_);
  }
  void MoveFrom(Variant &o) {
    static void (*const table[])(void *, void *) = {&MoveOne<Ts>...};
    table[o.index_](storage_, o.storage_);
  }

  alignas(detail::MaxSize<
          Ts...>::align) unsigned char storage_[detail::MaxSize<Ts...>::size];
  unsigned char index_;
};

template <typename T, typename... Ts>
VAPOR_NODISCARD bool HoldsAlternative(const Variant<Ts...> &v) noexcept {
  static_assert(detail::FindType<T, 0, Ts...>::matches == 1,
                "type must appear exactly once");
  return v.index() == detail::FindType<T, 0, Ts...>::index;
}

// Pointer to the value of alternative I, or nullptr if another one is active.
template <size_t I, typename... Ts>
VAPOR_NODISCARD typename detail::TypeAtImpl<I, Ts...>::type *
GetIf(Variant<Ts...> *v) noexcept {
  typedef typename detail::TypeAtImpl<I, Ts...>::type T;
  return v && v->index() == I ? static_cast<T *>(v->Raw()) : nullptr;
}
template <size_t I, typename... Ts>
VAPOR_NODISCARD const typename detail::TypeAtImpl<I, Ts...>::type *
GetIf(const Variant<Ts...> *v) noexcept {
  typedef typename detail::TypeAtImpl<I, Ts...>::type T;
  return v && v->index() == I ? static_cast<const T *>(v->Raw()) : nullptr;
}
template <typename T, typename... Ts>
VAPOR_NODISCARD T *GetIf(Variant<Ts...> *v) noexcept {
  static_assert(detail::FindType<T, 0, Ts...>::matches == 1,
                "type must appear exactly once");
  return GetIf<detail::FindType<T, 0, Ts...>::index>(v);
}
template <typename T, typename... Ts>
VAPOR_NODISCARD const T *GetIf(const Variant<Ts...> *v) noexcept {
  static_assert(detail::FindType<T, 0, Ts...>::matches == 1,
                "type must appear exactly once");
  return GetIf<detail::FindType<T, 0, Ts...>::index>(v);
}

// Reference to the value of alternative I; aborts if another one is active.
template <size_t I, typename... Ts>
VAPOR_NODISCARD typename detail::TypeAtImpl<I, Ts...>::type &
Get(Variant<Ts...> &v) noexcept {
  auto *p = GetIf<I>(&v);
  if (!p)
    abort();
  return *p;
}
template <size_t I, typename... Ts>
VAPOR_NODISCARD const typename detail::TypeAtImpl<I, Ts...>::type &
Get(const Variant<Ts...> &v) noexcept {
  auto *p = GetIf<I>(&v);
  if (!p)
    abort();
  return *p;
}
template <typename T, typename... Ts>
VAPOR_NODISCARD T &Get(Variant<Ts...> &v) noexcept {
  static_assert(detail::FindType<T, 0, Ts...>::matches == 1,
                "type must appear exactly once");
  return Get<detail::FindType<T, 0, Ts...>::index>(v);
}
template <typename T, typename... Ts>
VAPOR_NODISCARD const T &Get(const Variant<Ts...> &v) noexcept {
  static_assert(detail::FindType<T, 0, Ts...>::matches == 1,
                "type must appear exactly once");
  return Get<detail::FindType<T, 0, Ts...>::index>(v);
}

namespace detail {
template <typename R, typename F, typename V, size_t I> R VisitOne(F &f, V &v) {
  return f(*GetIf<I>(&v));
}
template <typename R, typename F, typename V, size_t... Is>
R VisitImpl(F &f, V &v, IndexSequence<Is...>) {
  static R (*const table[])(F &, V &) = {&VisitOne<R, F, V, Is>...};
  return table[v.index()](f, v);
}
} // namespace detail

// Calls `f` with the active alternative. `f` must accept every alternative and
// return the same type for each (a functor with several operator() overloads,
// or a generic lambda in C++14).
template <typename F, typename... Ts>
auto Visit(F &&f, Variant<Ts...> &v)
    -> decltype(f(Declval<typename detail::TypeAtImpl<0, Ts...>::type &>())) {
  typedef decltype(f(
      Declval<typename detail::TypeAtImpl<0, Ts...>::type &>())) R;
  return detail::VisitImpl<R, typename RemoveReference<F>::type,
                           Variant<Ts...>>(f, v,
                                           MakeIndexSequence<sizeof...(Ts)>());
}
template <typename F, typename... Ts>
auto Visit(F &&f, const Variant<Ts...> &v) -> decltype(f(
    Declval<const typename detail::TypeAtImpl<0, Ts...>::type &>())) {
  typedef decltype(f(
      Declval<const typename detail::TypeAtImpl<0, Ts...>::type &>())) R;
  return detail::VisitImpl<R, typename RemoveReference<F>::type,
                           const Variant<Ts...>>(
      f, v, MakeIndexSequence<sizeof...(Ts)>());
}

namespace detail {
template <typename V, size_t I> bool VariantEqOne(const V &a, const V &b) {
  return *GetIf<I>(&a) == *GetIf<I>(&b);
}
template <typename V, size_t I> bool VariantLtOne(const V &a, const V &b) {
  return *GetIf<I>(&a) < *GetIf<I>(&b);
}
template <typename V, size_t... Is>
bool VariantEq(const V &a, const V &b, IndexSequence<Is...>) {
  static bool (*const table[])(const V &,
                               const V &) = {&VariantEqOne<V, Is>...};
  return table[a.index()](a, b);
}
template <typename V, size_t... Is>
bool VariantLt(const V &a, const V &b, IndexSequence<Is...>) {
  static bool (*const table[])(const V &,
                               const V &) = {&VariantLtOne<V, Is>...};
  return table[a.index()](a, b);
}
} // namespace detail

template <typename... Ts>
bool operator==(const Variant<Ts...> &a, const Variant<Ts...> &b) {
  return a.index() == b.index() &&
         detail::VariantEq(a, b, MakeIndexSequence<sizeof...(Ts)>());
}
template <typename... Ts>
bool operator!=(const Variant<Ts...> &a, const Variant<Ts...> &b) {
  return !(a == b);
}
// Orders by active index first, then by value.
template <typename... Ts>
bool operator<(const Variant<Ts...> &a, const Variant<Ts...> &b) {
  if (a.index() != b.index())
    return a.index() < b.index();
  return detail::VariantLt(a, b, MakeIndexSequence<sizeof...(Ts)>());
}

template <typename... Ts> void Swap(Variant<Ts...> &a, Variant<Ts...> &b) {
  a.swap(b);
}

} // namespace vapor
