#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/type_traits.hpp>

// Checked and saturating integer arithmetic.
//
// Why: signed overflow is undefined behaviour in C++ and unsigned overflow
// silently wraps. Both are classic sources of security bugs (a size
// computation `count * sizeof(T)` that wraps to a small number, then a buffer
// that is too small). These helpers make the failure visible.
//
// How:
//   Expected<int, CommonError> r = CheckedAdd(a, b);   // ValueOutOfRange
//   int  s = SaturatingAdd(a, b);   // clamps to the type's min or max
//   auto n = CheckedCast<uint8_t>(300);                // ValueOutOfRange
//   auto q = CheckedDiv(a, 0);                         // InvalidArgument
//
// Both operands must have the same integer type (not bool). Nothing throws.
// Checked* return Expected; Saturating* return the clamped value.

namespace vapor {

template <typename T> struct IntLimits {
  static_assert(IsIntegral<T>::value && !IsSame<T, bool>::value,
                "integer type required");
  typedef MakeUnsignedT<T> U;
  static constexpr bool is_signed = IsSigned<T>::value;
  static constexpr T Max() {
    return is_signed ? static_cast<T>(static_cast<U>(~U(0)) >> 1)
                     : static_cast<T>(~U(0));
  }
  static constexpr T Min() {
    return is_signed ? static_cast<T>(-Max() - 1) : static_cast<T>(0);
  }
};

namespace detail {

template <typename T> struct Arith {
  typedef IntLimits<T> L;
  static constexpr bool Signed() { return IsSigned<T>::value; }

  // Neg(b): true when b is below zero (always false for unsigned types).
  static bool Neg(T v, TrueType) { return v < 0; }
  static bool Neg(T, FalseType) { return false; }
  static bool IsNeg(T v) {
    return Neg(v, IntegralConstant<bool, IsSigned<T>::value>());
  }

  static bool Add(T a, T b, T *out) {
    typedef MakeUnsignedT<T> U;
    // Wrap-around in the unsigned type is well defined; detect afterwards.
    T r = static_cast<T>(static_cast<U>(static_cast<U>(a) + static_cast<U>(b)));
    *out = r;
    if (IsNeg(b))
      return r > a; // adding a negative must not increase
    return r < a;
  }
  static bool Sub(T a, T b, T *out) {
    typedef MakeUnsignedT<T> U;
    T r = static_cast<T>(static_cast<U>(static_cast<U>(a) - static_cast<U>(b)));
    *out = r;
    if (IsNeg(b))
      return r < a; // subtracting a negative must not decrease
    return r > a;
  }
  static bool Mul(T a, T b, T *out) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, out);
#else
    typedef MakeUnsignedT<T> U;
    *out =
        static_cast<T>(static_cast<U>(static_cast<U>(a) * static_cast<U>(b)));
    if (a == 0 || b == 0)
      return false;
    if (IsNeg(a) != IsNeg(b)) {
      // Negative result: compare against Min using non-positive arithmetic.
      T aa = IsNeg(a) ? a : b, bb = IsNeg(a) ? b : a; // aa < 0 < bb
      return aa < L::Min() / bb;
    }
    if (IsNeg(a))
      return a < L::Max() / b; // both negative, product positive
    return a > L::Max() / b;
#endif
  }
};

// Result sign of a product, used to pick the saturation bound.
template <typename T> bool SameSign(T a, T b) {
  return Arith<T>::IsNeg(a) == Arith<T>::IsNeg(b);
}

// Range test for CheckedCast, split so the always-false comparison on
// unsigned types does not trigger warnings.
template <typename To, typename From> bool CastFits(From v) {
  To r = static_cast<To>(v);
  if (static_cast<From>(r) != v)
    return false;
  return Arith<To>::IsNeg(r) == Arith<From>::IsNeg(v);
}

} // namespace detail

template <typename T>
VAPOR_NODISCARD Expected<T, CommonError> CheckedAdd(T a, T b) noexcept {
  T r;
  if (detail::Arith<T>::Add(a, b, &r))
    return MakeUnexpected(CommonError::ValueOutOfRange);
  return r;
}
template <typename T>
VAPOR_NODISCARD Expected<T, CommonError> CheckedSub(T a, T b) noexcept {
  T r;
  if (detail::Arith<T>::Sub(a, b, &r))
    return MakeUnexpected(CommonError::ValueOutOfRange);
  return r;
}
template <typename T>
VAPOR_NODISCARD Expected<T, CommonError> CheckedMul(T a, T b) noexcept {
  T r;
  if (detail::Arith<T>::Mul(a, b, &r))
    return MakeUnexpected(CommonError::ValueOutOfRange);
  return r;
}
// Division: InvalidArgument for a zero divisor, ValueOutOfRange for
// Min() / -1 on signed types.
template <typename T>
VAPOR_NODISCARD Expected<T, CommonError> CheckedDiv(T a, T b) noexcept {
  if (b == 0)
    return MakeUnexpected(CommonError::InvalidArgument);
  if (IsSigned<T>::value && a == IntLimits<T>::Min() && b == static_cast<T>(-1))
    return MakeUnexpected(CommonError::ValueOutOfRange);
  return static_cast<T>(a / b);
}
template <typename T>
VAPOR_NODISCARD Expected<T, CommonError> CheckedRem(T a, T b) noexcept {
  if (b == 0)
    return MakeUnexpected(CommonError::InvalidArgument);
  if (IsSigned<T>::value && a == IntLimits<T>::Min() && b == static_cast<T>(-1))
    return static_cast<T>(0);
  return static_cast<T>(a % b);
}
// Negation (signed types; for unsigned only 0 can be negated).
template <typename T>
VAPOR_NODISCARD Expected<T, CommonError> CheckedNeg(T a) noexcept {
  return CheckedSub(static_cast<T>(0), a);
}

template <typename T> VAPOR_NODISCARD T SaturatingAdd(T a, T b) noexcept {
  T r;
  if (!detail::Arith<T>::Add(a, b, &r))
    return r;
  return detail::Arith<T>::IsNeg(b) ? IntLimits<T>::Min() : IntLimits<T>::Max();
}
template <typename T> VAPOR_NODISCARD T SaturatingSub(T a, T b) noexcept {
  T r;
  if (!detail::Arith<T>::Sub(a, b, &r))
    return r;
  return detail::Arith<T>::IsNeg(b) ? IntLimits<T>::Max() : IntLimits<T>::Min();
}
template <typename T> VAPOR_NODISCARD T SaturatingMul(T a, T b) noexcept {
  T r;
  if (!detail::Arith<T>::Mul(a, b, &r))
    return r;
  return detail::SameSign(a, b) ? IntLimits<T>::Max() : IntLimits<T>::Min();
}

// Converts between integer types, failing when the value does not fit
// (including negative -> unsigned).
template <typename To, typename From>
VAPOR_NODISCARD Expected<To, CommonError> CheckedCast(From v) noexcept {
  static_assert(IsIntegral<To>::value && IsIntegral<From>::value,
                "CheckedCast converts integers");
  if (!detail::CastFits<To, From>(v))
    return MakeUnexpected(CommonError::ValueOutOfRange);
  return static_cast<To>(v);
}

// Array/buffer size helper: count * size_of_element without wrapping.
VAPOR_NODISCARD inline Expected<size_t, CommonError>
CheckedArraySize(size_t count, size_t element_size) noexcept {
  return CheckedMul(count, element_size);
}

} // namespace vapor
