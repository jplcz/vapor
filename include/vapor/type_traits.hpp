#pragma once
#include <stddef.h>
#include <vapor/config.hpp>

// STL-free type traits (compile-time questions/transformations about types).
//
// Why: templates need to ask "is T trivially copyable?" or strip references
// from a type. <type_traits> normally answers that, but it is unavailable in
// freestanding/kernel builds. Everything here is C++11 and relies only on
// language features and compiler intrinsics (__is_base_of etc.).
//
// How: names are PascalCase. `Foo<T>::value` / `Foo<T>::type` mirror the
// standard layout, and `FooT<T>` is a shorthand for `typename Foo<T>::type`.

namespace vapor {

template <typename T, T V> struct IntegralConstant {
  static constexpr T value = V;
  using value_type = T;
  using type = IntegralConstant;
  constexpr operator value_type() const noexcept { return value; }
};

template <typename T, T V> constexpr T IntegralConstant<T, V>::value;

using TrueType = IntegralConstant<bool, true>;
using FalseType = IntegralConstant<bool, false>;

// --- Type selection ---
template <bool Cond, typename T = void> struct EnableIf {};
template <typename T> struct EnableIf<true, T> {
  using type = T;
};
template <bool Cond, typename T = void>
using EnableIfT = typename EnableIf<Cond, T>::type;

template <bool Cond, typename T, typename F> struct Conditional {
  using type = T;
};
template <typename T, typename F> struct Conditional<false, T, F> {
  using type = F;
};
template <bool Cond, typename T, typename F>
using ConditionalT = typename Conditional<Cond, T, F>::type;

template <typename T> struct Identity {
  using type = T;
};

// --- Type relationships ---
template <typename T, typename U> struct IsSame : FalseType {};
template <typename T> struct IsSame<T, T> : TrueType {};

// --- Reference / cv (const/volatile) modifiers ---
template <typename T> struct RemoveReference {
  using type = T;
};
template <typename T> struct RemoveReference<T &> {
  using type = T;
};
template <typename T> struct RemoveReference<T &&> {
  using type = T;
};
template <typename T>
using RemoveReferenceT = typename RemoveReference<T>::type;

template <typename T> struct RemoveConst {
  using type = T;
};
template <typename T> struct RemoveConst<const T> {
  using type = T;
};
template <typename T> struct RemoveVolatile {
  using type = T;
};
template <typename T> struct RemoveVolatile<volatile T> {
  using type = T;
};
template <typename T> struct RemoveCV {
  using type = typename RemoveVolatile<typename RemoveConst<T>::type>::type;
};
template <typename T> using RemoveCVT = typename RemoveCV<T>::type;

template <typename T> struct RemovePointer {
  using type = T;
};
template <typename T> struct RemovePointer<T *> {
  using type = T;
};
template <typename T> struct RemovePointer<T *const> {
  using type = T;
};
template <typename T> using RemovePointerT = typename RemovePointer<T>::type;

namespace detail {
template <typename...> struct MakeVoid {
  using type = void;
};
template <typename T, typename = void> struct AddRef {
  using lvalue = T;
  using rvalue = T;
};
template <typename T> struct AddRef<T, typename MakeVoid<T &>::type> {
  using lvalue = T &;
  using rvalue = T &&;
};
} // namespace detail

template <typename T> struct AddLvalueReference {
  using type = typename detail::AddRef<T>::lvalue;
};
template <typename T> struct AddRvalueReference {
  using type = typename detail::AddRef<T>::rvalue;
};
template <typename T>
using AddLvalueReferenceT = typename AddLvalueReference<T>::type;
template <typename T>
using AddRvalueReferenceT = typename AddRvalueReference<T>::type;

// Only declared: usable in unevaluated contexts (decltype/noexcept/sizeof).
template <typename T> typename AddRvalueReference<T>::type Declval() noexcept;

// --- Category queries ---
template <typename T> struct IsLvalueReference : FalseType {};
template <typename T> struct IsLvalueReference<T &> : TrueType {};
template <typename T> struct IsRvalueReference : FalseType {};
template <typename T> struct IsRvalueReference<T &&> : TrueType {};
template <typename T>
struct IsReference : IntegralConstant<bool, IsLvalueReference<T>::value ||
                                                IsRvalueReference<T>::value> {};

template <typename T> struct IsConst : FalseType {};
template <typename T> struct IsConst<const T> : TrueType {};

template <typename T> struct IsPointer : FalseType {};
template <typename T> struct IsPointer<T *> : TrueType {};
template <typename T> struct IsPointer<T *const> : TrueType {};
template <typename T> struct IsPointer<T *volatile> : TrueType {};
template <typename T> struct IsPointer<T *const volatile> : TrueType {};

template <typename T> struct IsArray : FalseType {};
template <typename T> struct IsArray<T[]> : TrueType {};
template <typename T, size_t N> struct IsArray<T[N]> : TrueType {};

template <typename T> struct RemoveExtent {
  using type = T;
};
template <typename T> struct RemoveExtent<T[]> {
  using type = T;
};
template <typename T, size_t N> struct RemoveExtent<T[N]> {
  using type = T;
};

template <typename T> struct IsFunction : FalseType {};
template <typename R, typename... A> struct IsFunction<R(A...)> : TrueType {};
template <typename R, typename... A>
struct IsFunction<R(A..., ...)> : TrueType {};

// Mirrors std::decay: by-value parameter type of an argument.
template <typename T> struct Decay {
private:
  using U = RemoveReferenceT<T>;

public:
  using type = typename Conditional<
      IsArray<U>::value, typename RemoveExtent<U>::type *,
      typename Conditional<IsFunction<U>::value, U *,
                           typename RemoveCV<U>::type>::type>::type;
};
template <typename T> using DecayT = typename Decay<T>::type;

// --- Compiler-intrinsic based queries ---
// These need compiler help because no portable C++ expresses them. Only
// intrinsics available on all of GCC 9, Clang and MSVC are used, so the
// minimum supported compiler is GCC 9 era.
#if defined(__clang__) || defined(__GNUC__) || defined(_MSC_VER)
#define VAPOR_HAS_TYPE_INTRINSICS 1
#endif

#ifdef VAPOR_HAS_TYPE_INTRINSICS
#define VAPOR_TRAIT_1(Name, Intrinsic)                                         \
  template <typename T> struct Name : IntegralConstant<bool, Intrinsic(T)> {}

// Layout / lifetime properties
VAPOR_TRAIT_1(IsTriviallyCopyable, __is_trivially_copyable);
VAPOR_TRAIT_1(IsTriviallyDestructible, __has_trivial_destructor);
VAPOR_TRAIT_1(IsTrivial, __is_trivial);
VAPOR_TRAIT_1(IsStandardLayout, __is_standard_layout);
VAPOR_TRAIT_1(IsPod, __is_pod);
VAPOR_TRAIT_1(IsEmpty, __is_empty);
VAPOR_TRAIT_1(IsAggregate, __is_aggregate);

// Type categories
VAPOR_TRAIT_1(IsEnum, __is_enum);
VAPOR_TRAIT_1(IsUnion, __is_union);
VAPOR_TRAIT_1(IsClass, __is_class);
VAPOR_TRAIT_1(IsPolymorphic, __is_polymorphic);
VAPOR_TRAIT_1(IsAbstract, __is_abstract);
VAPOR_TRAIT_1(IsFinal, __is_final);

#undef VAPOR_TRAIT_1

template <typename B, typename D>
struct IsBaseOf : IntegralConstant<bool, __is_base_of(B, D)> {};

template <typename T, typename... Args>
struct IsTriviallyConstructible
    : IntegralConstant<bool, __is_trivially_constructible(T, Args...)> {};
template <typename T>
struct IsTriviallyDefaultConstructible : IsTriviallyConstructible<T> {};

template <typename T, typename U>
struct IsTriviallyAssignable
    : IntegralConstant<bool, __is_trivially_assignable(T, U)> {};

template <typename T, typename... Args>
struct IsNothrowConstructible
    : IntegralConstant<bool, __is_nothrow_constructible(T, Args...)> {};

template <typename T, typename U>
struct IsNothrowAssignable
    : IntegralConstant<bool, __is_nothrow_assignable(T, U)> {};

// Underlying integer type of an enum (valid only when IsEnum<T>).
template <typename T> struct UnderlyingType {
  using type = __underlying_type(T);
};
template <typename T> using UnderlyingTypeT = typename UnderlyingType<T>::type;
#endif

namespace detail {
template <typename From, typename To>
auto IsConvertibleTest(int)
    -> decltype(static_cast<void (*)(To)>(nullptr)(Declval<From>()),
                TrueType());
template <typename From, typename To> FalseType IsConvertibleTest(...);

template <typename T, typename... Args>
auto IsConstructibleTest(int) -> decltype(T(Declval<Args>()...), TrueType());
template <typename T, typename... Args> FalseType IsConstructibleTest(...);
} // namespace detail

template <typename From, typename To>
struct IsConvertible : decltype(detail::IsConvertibleTest<From, To>(0)) {};

template <typename T, typename... Args>
struct IsConstructible : decltype(detail::IsConstructibleTest<T, Args...>(0)) {
};

template <typename T>
struct IsNothrowMoveConstructible
    : IntegralConstant<bool, noexcept(T(Declval<T>()))> {};

template <typename T>
struct IsCopyConstructible : IsConstructible<T, const T &> {};

namespace detail {
template <typename T> struct IsIntegralBase : FalseType {};
#define VAPOR_INTEGRAL(T)                                                      \
  template <> struct IsIntegralBase<T> : TrueType {}
VAPOR_INTEGRAL(bool);
VAPOR_INTEGRAL(char);
VAPOR_INTEGRAL(signed char);
VAPOR_INTEGRAL(unsigned char);
VAPOR_INTEGRAL(wchar_t);
VAPOR_INTEGRAL(char16_t);
VAPOR_INTEGRAL(char32_t);
VAPOR_INTEGRAL(short);
VAPOR_INTEGRAL(unsigned short);
VAPOR_INTEGRAL(int);
VAPOR_INTEGRAL(unsigned int);
VAPOR_INTEGRAL(long);
VAPOR_INTEGRAL(unsigned long);
VAPOR_INTEGRAL(long long);
VAPOR_INTEGRAL(unsigned long long);
#if defined(__cpp_char8_t)
VAPOR_INTEGRAL(char8_t);
#endif
#undef VAPOR_INTEGRAL

template <typename T> struct IsFloatingBase : FalseType {};
template <> struct IsFloatingBase<float> : TrueType {};
template <> struct IsFloatingBase<double> : TrueType {};
template <> struct IsFloatingBase<long double> : TrueType {};
} // namespace detail

template <typename T>
struct IsIntegral : detail::IsIntegralBase<RemoveCVT<T>> {};
template <typename T>
struct IsFloatingPoint : detail::IsFloatingBase<RemoveCVT<T>> {};
template <typename T>
struct IsArithmetic : IntegralConstant<bool, IsIntegral<T>::value ||
                                                 IsFloatingPoint<T>::value> {};
template <typename T>
struct IsNullPointer : IsSame<RemoveCVT<T>, decltype(nullptr)> {};

namespace detail {
template <typename T, bool = IsArithmetic<T>::value>
struct IsSignedBase : FalseType {};
template <typename T>
struct IsSignedBase<T, true> : IntegralConstant<bool, T(-1) < T(0)> {};
template <typename T, bool = IsArithmetic<T>::value>
struct IsUnsignedBase : FalseType {};
template <typename T>
struct IsUnsignedBase<T, true> : IntegralConstant<bool, T(0) < T(-1)> {};
} // namespace detail

template <typename T> struct IsSigned : detail::IsSignedBase<T> {};
template <typename T> struct IsUnsigned : detail::IsUnsignedBase<T> {};

// The unsigned integer type with the same size as T (T integral or enum).
// Useful for bit manipulation without worrying about sign.
template <typename T> struct MakeUnsigned {
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                    sizeof(T) == 8,
                "MakeUnsigned supports 1, 2, 4 and 8 byte types");
  typedef ConditionalT<sizeof(T) == 1, unsigned char,
                       ConditionalT<sizeof(T) == 2, unsigned short,
                                    ConditionalT<sizeof(T) == 4, unsigned int,
                                                 unsigned long long>>>
      type;
};
template <typename T> using MakeUnsignedT = typename MakeUnsigned<T>::type;

} // namespace vapor
