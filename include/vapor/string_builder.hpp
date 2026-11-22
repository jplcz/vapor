#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/string.hpp>
#include <vapor/string_view.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// StringBuilder: builds one String out of many pieces (text, characters,
// numbers).
//
// Why: assembling "user 42 logged in from 10.0.0.1" with String::append means
// converting the number to text by hand first, and every step can run out of
// memory. StringBuilder knows how to append numbers and booleans without
// printf and without a temporary buffer the caller must size, grows its buffer
// geometrically so repeated appends stay cheap, and reports allocation failure
// through Expected like the rest of the library (it never throws).
//
// How:
//
//   StringBuilder sb;
//   Expected<void, CommonError> r =
//       sb.AppendAll("user ", 42, " logged in, ok=", true, '\n');
//   if (!r) return r;                // OutOfMemory; sb still holds the prefix
//   String text = sb.Take();         // moves the text out, sb is empty again
//
// Append() adds one value; AppendAll() adds several in order and stops at the
// first failure. Characters are appended as characters, integers as decimal
// numbers, bool as "true"/"false", const char* / StringView / String as text.
// Use AppendInt()/AppendHex() for other bases and AppendRepeat() for padding.
//
// Floating-point values go through the C library's snprintf, so the decimal
// separator follows the C locale (the default "C" locale prints '.').

namespace vapor {

class StringBuilder {
  Allocator *m_alloc;
  String m_str;

  // Absolute value as uint64_t. The tag overloads keep the "< 0" test out of
  // unsigned instantiations, where it would trigger a warning.
  template <typename T>
  static uint64_t Magnitude(T v, bool &negative, TrueType /*is_signed*/) {
    negative = v < 0;
    uint64_t wide = static_cast<uint64_t>(static_cast<int64_t>(v));
    return negative ? 0 - wide : wide;
  }
  template <typename T>
  static uint64_t Magnitude(T v, bool &negative, FalseType /*is_signed*/) {
    negative = false;
    return static_cast<uint64_t>(v);
  }
  template <typename T> static uint64_t Magnitude(T v, bool &negative) {
    return Magnitude(v, negative,
                     IntegralConstant<bool, IsSigned<T>::value>());
  }

  Expected<void, CommonError> AppendNumber(uint64_t mag, bool negative,
                                           int base, bool upper) {
    if (base < 2 || base > 36)
      return MakeUnexpected(CommonError::InvalidArgument);
    // 64 binary digits plus the sign is the longest possible output.
    char buf[65];
    size_t pos = sizeof(buf);
    const char *digits =
        upper ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              : "0123456789abcdefghijklmnopqrstuvwxyz";
    const uint64_t ubase = static_cast<uint64_t>(base);
    do {
      buf[--pos] = digits[mag % ubase];
      mag /= ubase;
    } while (mag != 0);
    if (negative)
      buf[--pos] = '-';
    return m_str.append(StringView(buf + pos, sizeof(buf) - pos));
  }

public:
  explicit StringBuilder(Allocator &alloc = DefaultAllocator::GetDefault())
      : m_alloc(&alloc), m_str(alloc) {}

  StringBuilder(StringBuilder &&) = default;
  StringBuilder &operator=(StringBuilder &&) = default;
  StringBuilder(const StringBuilder &) = delete;
  StringBuilder &operator=(const StringBuilder &) = delete;

  // Makes room for `n` characters in total so later appends do not allocate.
  Expected<void, CommonError> reserve(size_t n) { return m_str.reserve(n); }

  // --- appending text

  Expected<void, CommonError> Append(StringView sv) { return m_str.append(sv); }
  Expected<void, CommonError> Append(const char *s) {
    return s ? m_str.append(StringView(s)) : Expected<void, CommonError>();
  }
  Expected<void, CommonError> Append(char c) { return m_str.push_back(c); }
  Expected<void, CommonError> Append(bool b) {
    return m_str.append(b ? StringView("true") : StringView("false"));
  }
  Expected<void, CommonError> Append(const String &s) {
    return m_str.append(StringView(s));
  }

  // Any integer type other than char and bool: decimal digits.
  template <typename T,
            EnableIfT<IsIntegral<T>::value && !IsSame<T, char>::value &&
                          !IsSame<T, bool>::value,
                      int> = 0>
  Expected<void, CommonError> Append(T v) {
    bool neg;
    uint64_t mag = Magnitude(v, neg);
    return AppendNumber(mag, neg, 10, false);
  }

  // Floating point, `precision` significant digits (like printf "%.{p}g").
  Expected<void, CommonError> Append(double v, int precision = 6) {
    char buf[64];
    if (precision < 1)
      precision = 1;
    if (precision > 40)
      precision = 40;
    int n = ::snprintf(buf, sizeof(buf), "%.*g", precision, v);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buf))
      return MakeUnexpected(CommonError::InvalidArgument);
    return m_str.append(StringView(buf, static_cast<size_t>(n)));
  }
  Expected<void, CommonError> Append(float v) {
    return Append(static_cast<double>(v), 6);
  }

  // Integer in base 2..36 (InvalidArgument otherwise). Negative values get a
  // '-' sign, never a two's complement representation.
  template <typename T>
  Expected<void, CommonError> AppendInt(T v, int base = 10,
                                        bool upper = false) {
    static_assert(IsIntegral<T>::value, "AppendInt needs an integer type");
    bool neg;
    uint64_t mag = Magnitude(v, neg);
    return AppendNumber(mag, neg, base, upper);
  }

  // Hexadecimal digits of the value's bit pattern, padded with '0' to at
  // least `min_digits` (no "0x" prefix).
  template <typename T>
  Expected<void, CommonError> AppendHex(T v, size_t min_digits = 1,
                                        bool upper = false) {
    static_assert(IsIntegral<T>::value, "AppendHex needs an integer type");
    typedef MakeUnsignedT<T> U;
    uint64_t bits = static_cast<uint64_t>(static_cast<U>(v));
    size_t digits = 1;
    for (uint64_t t = bits >> 4; t != 0; t >>= 4)
      ++digits;
    if (digits < min_digits) {
      Expected<void, CommonError> r = AppendRepeat('0', min_digits - digits);
      if (!r)
        return r;
    }
    return AppendNumber(bits, false, 16, upper);
  }

  Expected<void, CommonError> AppendRepeat(char c, size_t count) {
    if (count == 0)
      return {};
    Expected<void, CommonError> r = m_str.reserve(m_str.size() + count);
    if (!r)
      return r;
    for (size_t i = 0; i < count; ++i) {
      r = m_str.push_back(c);
      if (!r)
        return r;
    }
    return {};
  }

  // Appends each element of `range` (anything Append() accepts), separated by
  // `sep`. On failure the elements appended so far stay.
  template <typename C>
  Expected<void, CommonError> AppendJoin(const C &range, StringView sep) {
    bool first = true;
    for (const auto &item : range) {
      if (!first) {
        Expected<void, CommonError> rs = Append(sep);
        if (!rs)
          return rs;
      }
      first = false;
      Expected<void, CommonError> r = Append(item);
      if (!r)
        return r;
    }
    return {};
  }

  // Appends all arguments in order; stops at the first failure.
  Expected<void, CommonError> AppendAll() { return {}; }
  template <typename A, typename... Rest>
  Expected<void, CommonError> AppendAll(const A &first, const Rest &...rest) {
    Expected<void, CommonError> r = Append(first);
    if (!r)
      return r;
    return AppendAll(rest...);
  }

  // --- reading the result

  VAPOR_NODISCARD size_t size() const noexcept { return m_str.size(); }
  VAPOR_NODISCARD size_t capacity() const noexcept { return m_str.capacity(); }
  VAPOR_NODISCARD bool empty() const noexcept { return m_str.empty(); }
  VAPOR_NODISCARD const char *c_str() const noexcept { return m_str.c_str(); }
  // Valid until the next append.
  VAPOR_NODISCARD StringView view() const noexcept {
    return StringView(m_str.data(), m_str.size());
  }

  void clear() noexcept { m_str.clear(); }

  // Moves the built text out; the builder is empty (and reusable) afterwards.
  VAPOR_NODISCARD String Take() {
    String out(Move(m_str));
    m_str = String(*m_alloc);
    return out;
  }
};

} // namespace vapor
