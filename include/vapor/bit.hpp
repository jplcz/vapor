#pragma once
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vapor/config.hpp>
#include <vapor/type_traits.hpp>

// Bit manipulation helpers, modelled on C++20 <bit> but usable from C++11 and
// without the STL.
//
// Why: counting set bits, finding the lowest set bit, rotating or rounding up
// to a power of two are everyday needs in hash tables, allocators, bitsets
// and protocol code. Compilers expose fast instructions for them, but only
// through compiler-specific builtins; these wrappers pick the builtin where
// there is one and fall back to portable code otherwise.
//
// How: all functions take an unsigned integer (unsigned char ... unsigned long
// long) and return an `int` count or the same type back.
// `CountTrailingZeros(0u)` is 32 (the bit width), never undefined.
// `BitCast<To>(from)` reinterprets the bytes of a value as another type of the
// same size (e.g. float <-> uint32_t) without the undefined behaviour of a
// pointer cast.

namespace vapor {

namespace detail {

template <typename T>
using EnableIfUnsignedInt =
    EnableIfT<IsIntegral<T>::value && IsUnsigned<T>::value &&
                  !IsSame<RemoveCVT<T>, bool>::value,
              int>;

template <typename T> struct BitDigits {
  static constexpr int value = static_cast<int>(sizeof(T) * CHAR_BIT);
};

// Nonzero-input primitives on 32 and 64 bits.
inline int Pop32(uint32_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcount(v);
#else
  v = v - ((v >> 1) & 0x55555555u);
  v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
  return static_cast<int>((((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
#endif
}
inline int Pop64(uint64_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(v);
#else
  return Pop32(static_cast<uint32_t>(v)) +
         Pop32(static_cast<uint32_t>(v >> 32));
#endif
}
inline int Ctz32(uint32_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctz(v);
#else
  int n = 0;
  while (!(v & 1u)) {
    v >>= 1;
    ++n;
  }
  return n;
#endif
}
inline int Ctz64(uint64_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(v);
#else
  return (static_cast<uint32_t>(v) != 0)
             ? Ctz32(static_cast<uint32_t>(v))
             : 32 + Ctz32(static_cast<uint32_t>(v >> 32));
#endif
}
inline int Clz32(uint32_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_clz(v);
#else
  int n = 0;
  while (!(v & 0x80000000u)) {
    v <<= 1;
    ++n;
  }
  return n;
#endif
}
inline int Clz64(uint64_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_clzll(v);
#else
  return (v >> 32) ? Clz32(static_cast<uint32_t>(v >> 32))
                   : 32 + Clz32(static_cast<uint32_t>(v));
#endif
}

} // namespace detail

// Number of set bits.
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline int PopCount(T x) noexcept {
  return sizeof(T) <= 4 ? detail::Pop32(static_cast<uint32_t>(x))
                        : detail::Pop64(static_cast<uint64_t>(x));
}

// Number of consecutive zero bits from the least significant end; the bit
// width of T when x is 0.
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline int CountTrailingZeros(T x) noexcept {
  if (x == 0)
    return detail::BitDigits<T>::value;
  return sizeof(T) <= 4 ? detail::Ctz32(static_cast<uint32_t>(x))
                        : detail::Ctz64(static_cast<uint64_t>(x));
}

// Number of consecutive zero bits from the most significant end; the bit width
// of T when x is 0.
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline int CountLeadingZeros(T x) noexcept {
  if (x == 0)
    return detail::BitDigits<T>::value;
  return sizeof(T) <= 4 ? detail::Clz32(static_cast<uint32_t>(x)) -
                              (32 - detail::BitDigits<T>::value)
                        : detail::Clz64(static_cast<uint64_t>(x));
}

template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline int CountTrailingOnes(T x) noexcept {
  return CountTrailingZeros(static_cast<T>(~x));
}
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline int CountLeadingOnes(T x) noexcept {
  return CountLeadingZeros(static_cast<T>(~x));
}

// Smallest number of bits needed to represent x (0 for x == 0).
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline int BitWidth(T x) noexcept {
  return detail::BitDigits<T>::value - CountLeadingZeros(x);
}

// True when exactly one bit is set.
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD constexpr bool IsPowerOfTwo(T x) noexcept {
  return x != 0 && (x & static_cast<T>(x - 1)) == 0;
}

// Largest power of two not greater than x (0 for x == 0).
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline T BitFloor(T x) noexcept {
  return x == 0 ? T(0) : static_cast<T>(T(1) << (BitWidth(x) - 1));
}

// Smallest power of two not less than x (1 for x == 0). Returns 0 when the
// result does not fit in T, because there are no exceptions to report it.
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline T BitCeil(T x) noexcept {
  if (x <= 1)
    return T(1);
  int w = BitWidth(static_cast<T>(x - 1));
  if (w >= detail::BitDigits<T>::value)
    return T(0);
  return static_cast<T>(T(1) << w);
}

// Rotates the bits left (or right) by `shift`; shift is taken modulo the
// bit width and may be negative to rotate the other way.
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline T RotateLeft(T x, int shift) noexcept {
  const int bits = detail::BitDigits<T>::value;
  int s = shift % bits;
  if (s < 0)
    s += bits;
  if (s == 0)
    return x;
  return static_cast<T>((x << s) | (x >> (bits - s)));
}
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline T RotateRight(T x, int shift) noexcept {
  return RotateLeft(x, -(shift % detail::BitDigits<T>::value));
}

// Reverses the byte order (big-endian <-> little-endian).
template <typename T, detail::EnableIfUnsignedInt<T> = 0>
VAPOR_NODISCARD inline T ByteSwap(T x) noexcept {
  uint64_t r = 0; // wide so small T never goes through signed int promotion
  uint64_t v = x;
  for (size_t i = 0; i < sizeof(T); ++i) {
    r = (r << CHAR_BIT) | (v & 0xFFu);
    v >>= CHAR_BIT;
  }
  return static_cast<T>(r);
}

// Reinterprets the object representation of `from` as a `To`.
template <
    typename To, typename From,
    EnableIfT<sizeof(To) == sizeof(From) && IsTriviallyCopyable<To>::value &&
                  IsTriviallyCopyable<From>::value,
              int> = 0>
VAPOR_NODISCARD inline To BitCast(const From &from) noexcept {
  To to;
  ::memcpy(static_cast<void *>(&to), static_cast<const void *>(&from),
           sizeof(To));
  return to;
}

} // namespace vapor
