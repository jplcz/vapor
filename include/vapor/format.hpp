#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/string.hpp>
#include <vapor/string_builder.hpp>
#include <vapor/string_view.hpp>
#include <vapor/type_traits.hpp>

// Format: type-safe text formatting with "{}" placeholders (the same syntax
// as C++20 std::format / Python), without exceptions or the STL.
//
// Why: printf is not type safe (a wrong %d is undefined behaviour) and
// building text by hand with many Append calls is noisy. Here the argument
// types are known at compile time, so there is nothing to get wrong, and an
// out-of-memory condition or a malformed format string is reported through
// Expected instead of crashing.
//
// How:
//   Expected<String, CommonError> s = Format("{} has {} items", name, count);
//   FormatTo(builder, "{:08.3f}|{:>6}|{:#x}", 3.14159, "ab", 255);
//                  // 0003.142|    ab|0xff
//
// A placeholder is `{[index][:spec]}`. `{}` takes the next argument, `{1}`
// takes argument 1 (counting from 0). `{{` and `}}` print braces.
// spec = [[fill]align][sign][#][0][width][.precision][type]
//   fill       any single character (default space)
//   align      `<` left, `>` right, `^` centre (default: left for text,
//              right for numbers)
//   sign       `+` always show a sign, ` ` space for non-negative numbers
//   #          alternative form: 0x / 0b / 0 prefix for integers
//   0          pad numbers with zeros after the sign and prefix
//   width      minimum width in bytes (not UTF-8 characters)
//   .precision digits after the point for floats; maximum bytes for text
//   type       integers: d x X b o c      floats: f F e E g G
//              text: s     pointers: p     (bool prints true/false)
//
// Supported arguments: integers, bool, char, float/double, const char*,
// StringView, String, and pointers (printed as an address). Errors:
// InvalidArgument for a malformed string, an argument index that does not
// exist, or a type letter that does not fit the argument; OutOfMemory when
// the builder cannot grow (it then keeps the part written so far).
// Floating point output uses the C library, so it follows the C locale. By
// default a float prints its shortest text that reads back as the same value.

namespace vapor {

class FormatArg {
public:
  enum Kind { kInt, kUInt, kBool, kChar, kDouble, kString, kPointer };

private:
  Kind m_kind;
  int64_t m_i;
  uint64_t m_u;
  double m_d;
  const void *m_p;
  StringView m_s;

public:
  FormatArg() noexcept
      : m_kind(kInt), m_i(0), m_u(0), m_d(0), m_p(nullptr), m_s() {}

  template <typename T,
            EnableIfT<IsIntegral<T>::value && IsSigned<T>::value &&
                          !IsSame<T, char>::value && !IsSame<T, bool>::value,
                      int> = 0>
  FormatArg(T v) noexcept : FormatArg() {
    m_kind = kInt;
    m_i = static_cast<int64_t>(v);
  }
  template <typename T,
            EnableIfT<IsIntegral<T>::value && !IsSigned<T>::value &&
                          !IsSame<T, char>::value && !IsSame<T, bool>::value,
                      int> = 0>
  FormatArg(T v) noexcept : FormatArg() {
    m_kind = kUInt;
    m_u = static_cast<uint64_t>(v);
  }
  template <typename T, EnableIfT<IsSame<T, bool>::value, int> = 0>
  FormatArg(T v) noexcept : FormatArg() {
    m_kind = kBool;
    m_u = v ? 1 : 0;
  }
  template <typename T, EnableIfT<IsSame<T, char>::value, int> = 0>
  FormatArg(T v) noexcept : FormatArg() {
    m_kind = kChar;
    m_u = static_cast<unsigned char>(v);
  }
  template <typename T, EnableIfT<IsFloatingPoint<T>::value, int> = 0>
  FormatArg(T v) noexcept : FormatArg() {
    m_kind = kDouble;
    m_d = static_cast<double>(v);
  }
  FormatArg(const char *s) noexcept : FormatArg() {
    m_kind = kString;
    m_s = s ? StringView(s) : StringView();
  }
  FormatArg(char *s) noexcept : FormatArg(static_cast<const char *>(s)) {}
  FormatArg(StringView s) noexcept : FormatArg() {
    m_kind = kString;
    m_s = s;
  }
  FormatArg(const String &s) noexcept : FormatArg() {
    m_kind = kString;
    m_s = StringView(s.data(), s.size());
  }
  FormatArg(const void *p) noexcept : FormatArg() {
    m_kind = kPointer;
    m_p = p;
  }
  FormatArg(decltype(nullptr)) noexcept : FormatArg() { m_kind = kPointer; }

  Kind kind() const noexcept { return m_kind; }
  int64_t AsInt() const noexcept { return m_i; }
  uint64_t AsUInt() const noexcept { return m_u; }
  double AsDouble() const noexcept { return m_d; }
  const void *AsPointer() const noexcept { return m_p; }
  StringView AsString() const noexcept { return m_s; }
};

namespace detail {

struct FormatSpec {
  char fill;
  char align; // 0 = default, '<', '>', '^'
  bool plus;
  bool space;
  bool alt;
  bool zero;
  size_t width;
  int precision; // -1 = none
  char type;     // 0 = default
};

inline bool FmtIsDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// Parses a run of decimal digits; false on overflow.
inline bool FmtParseNumber(StringView s, size_t &pos, size_t &out) noexcept {
  out = 0;
  while (pos < s.size() && FmtIsDigit(s[pos])) {
    size_t d = static_cast<size_t>(s[pos] - '0');
    if (out > (VAPOR_SIZE_MAX - d) / 10)
      return false;
    out = out * 10 + d;
    ++pos;
  }
  return true;
}

inline bool FmtParseSpec(StringView s, FormatSpec &sp) noexcept {
  sp.fill = ' ';
  sp.align = 0;
  sp.plus = sp.space = sp.alt = sp.zero = false;
  sp.width = 0;
  sp.precision = -1;
  sp.type = 0;
  size_t i = 0;
  auto is_align = [](char c) { return c == '<' || c == '>' || c == '^'; };
  if (s.size() >= 2 && is_align(s[1])) {
    sp.fill = s[0];
    sp.align = s[1];
    i = 2;
  } else if (!s.empty() && is_align(s[0])) {
    sp.align = s[0];
    i = 1;
  }
  if (i < s.size() && (s[i] == '+' || s[i] == ' ')) {
    (s[i] == '+' ? sp.plus : sp.space) = true;
    ++i;
  }
  if (i < s.size() && s[i] == '#') {
    sp.alt = true;
    ++i;
  }
  if (i < s.size() && s[i] == '0') {
    sp.zero = true;
    ++i;
  }
  if (!FmtParseNumber(s, i, sp.width))
    return false;
  if (i < s.size() && s[i] == '.') {
    ++i;
    size_t p = 0;
    size_t start = i;
    if (!FmtParseNumber(s, i, p) || i == start || p > 99)
      return false;
    sp.precision = static_cast<int>(p);
  }
  if (i < s.size()) {
    sp.type = s[i];
    ++i;
  }
  return i == s.size();
}

// Writes `sign`, `prefix`, zero padding, `body` with the padding rules of spec.
inline Expected<void, CommonError>
FmtEmit(StringBuilder &sb, const FormatSpec &sp, char default_align,
        StringView sign, StringView prefix, StringView body, bool numeric) {
  size_t len = sign.size() + prefix.size() + body.size();
  size_t zeros = 0, left = 0, right = 0;
  if (sp.width > len) {
    size_t pad = sp.width - len;
    if (numeric && sp.zero && sp.align == 0) {
      zeros = pad;
    } else {
      char a = sp.align ? sp.align : default_align;
      if (a == '<')
        right = pad;
      else if (a == '>')
        left = pad;
      else {
        left = pad / 2;
        right = pad - left;
      }
    }
  }
  Expected<void, CommonError> r = sb.AppendRepeat(sp.fill, left);
  if (r)
    r = sb.Append(sign);
  if (r)
    r = sb.Append(prefix);
  if (r)
    r = sb.AppendRepeat('0', zeros);
  if (r)
    r = sb.Append(body);
  if (r)
    r = sb.AppendRepeat(sp.fill, right);
  return r;
}

inline Expected<void, CommonError> FmtInteger(StringBuilder &sb,
                                              const FormatSpec &sp,
                                              bool negative, uint64_t mag) {
  unsigned base = 10;
  bool upper = false;
  const char *prefix = "";
  switch (sp.type) {
  case 0:
  case 'd':
    break;
  case 'x':
    base = 16;
    prefix = "0x";
    break;
  case 'X':
    base = 16;
    upper = true;
    prefix = "0X";
    break;
  case 'b':
    base = 2;
    prefix = "0b";
    break;
  case 'o':
    base = 8;
    prefix = "0";
    break;
  default:
    return MakeUnexpected(CommonError::InvalidArgument);
  }
  char buf[72];
  size_t pos = sizeof(buf);
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  do {
    buf[--pos] = digits[mag % base];
    mag /= base;
  } while (mag != 0);
  StringView sign = negative ? "-" : sp.plus ? "+" : sp.space ? " " : "";
  return FmtEmit(sb, sp, '>', sign, sp.alt && base != 10 ? prefix : "",
                 StringView(buf + pos, sizeof(buf) - pos), true);
}

inline Expected<void, CommonError> FmtDouble(StringBuilder &sb,
                                             const FormatSpec &sp, double v) {
  char conv = sp.type;
  if (conv != 0 && conv != 'f' && conv != 'F' && conv != 'e' && conv != 'E' &&
      conv != 'g' && conv != 'G')
    return MakeUnexpected(CommonError::InvalidArgument);
  bool negative = v < 0 || (v == 0 && 1.0 / v < 0);
  double mag = negative ? -v : v;
  bool finite = (mag == mag) && mag <= 1.7976931348623157e308;
  char buf[512];
  int n = 0;
  if (conv == 0 && sp.precision < 0) {
    // Shortest representation that reads back to the same double.
    for (int prec = 1; prec <= 17; ++prec) {
      n = snprintf(buf, sizeof(buf), "%.*g", prec, mag);
      if (strtod(buf, nullptr) == mag)
        break;
    }
  } else {
    char c = conv ? conv : 'g';
    char fmt[8] = {'%', '.', '*', c, 0};
    int prec = sp.precision < 0 ? 6 : sp.precision;
    n = snprintf(buf, sizeof(buf), fmt, prec, mag);
  }
  if (n < 0 || static_cast<size_t>(n) >= sizeof(buf))
    return MakeUnexpected(CommonError::InvalidArgument);
  FormatSpec local = sp;
  if (!finite)
    local.zero = false;
  // NaN has no sign.
  bool show_neg = negative && mag == mag;
  StringView sign = show_neg ? "-" : sp.plus ? "+" : sp.space ? " " : "";
  return FmtEmit(sb, local, '>', sign, "",
                 StringView(buf, static_cast<size_t>(n)), true);
}

inline Expected<void, CommonError>
FmtOne(StringBuilder &sb, const FormatSpec &sp, const FormatArg &a) {
  switch (a.kind()) {
  case FormatArg::kInt: {
    int64_t v = a.AsInt();
    if (sp.type == 'c') {
      char c = static_cast<char>(v);
      return FmtEmit(sb, sp, '<', "", "", StringView(&c, 1), false);
    }
    bool neg = v < 0;
    uint64_t mag =
        neg ? (0 - static_cast<uint64_t>(v)) : static_cast<uint64_t>(v);
    return FmtInteger(sb, sp, neg, mag);
  }
  case FormatArg::kUInt:
    if (sp.type == 'c') {
      char c = static_cast<char>(a.AsUInt());
      return FmtEmit(sb, sp, '<', "", "", StringView(&c, 1), false);
    }
    return FmtInteger(sb, sp, false, a.AsUInt());
  case FormatArg::kBool:
    if (sp.type == 0 || sp.type == 's') {
      StringView t = a.AsUInt() ? "true" : "false";
      return FmtEmit(sb, sp, '<', "", "", t, false);
    }
    return FmtInteger(sb, sp, false, a.AsUInt());
  case FormatArg::kChar: {
    if (sp.type == 0 || sp.type == 'c') {
      char c = static_cast<char>(a.AsUInt());
      return FmtEmit(sb, sp, '<', "", "", StringView(&c, 1), false);
    }
    return FmtInteger(sb, sp, false, a.AsUInt());
  }
  case FormatArg::kDouble:
    return FmtDouble(sb, sp, a.AsDouble());
  case FormatArg::kString: {
    if (sp.type != 0 && sp.type != 's')
      return MakeUnexpected(CommonError::InvalidArgument);
    StringView s = a.AsString();
    if (sp.precision >= 0 && s.size() > static_cast<size_t>(sp.precision))
      s = StringView(s.data(), static_cast<size_t>(sp.precision));
    return FmtEmit(sb, sp, '<', "", "", s, false);
  }
  case FormatArg::kPointer: {
    if (sp.type != 0 && sp.type != 'p')
      return MakeUnexpected(CommonError::InvalidArgument);
    FormatSpec hex = sp;
    hex.type = 'x';
    hex.alt = true;
    return FmtInteger(
        sb, hex, false,
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(a.AsPointer())));
  }
  }
  return MakeUnexpected(CommonError::InvalidArgument);
}

} // namespace detail

// Non-template core: formats `fmt` with `n` arguments into `sb`.
inline Expected<void, CommonError> VFormatTo(StringBuilder &sb, StringView fmt,
                                             const FormatArg *args, size_t n) {
  size_t next_auto = 0;
  size_t i = 0;
  while (i < fmt.size()) {
    char c = fmt[i];
    if (c == '}') {
      if (i + 1 < fmt.size() && fmt[i + 1] == '}') {
        Expected<void, CommonError> r = sb.Append('}');
        if (!r)
          return r;
        i += 2;
        continue;
      }
      return MakeUnexpected(CommonError::InvalidArgument);
    }
    if (c != '{') {
      size_t start = i;
      while (i < fmt.size() && fmt[i] != '{' && fmt[i] != '}')
        ++i;
      Expected<void, CommonError> r =
          sb.Append(StringView(fmt.data() + start, i - start));
      if (!r)
        return r;
      continue;
    }
    if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
      Expected<void, CommonError> r = sb.Append('{');
      if (!r)
        return r;
      i += 2;
      continue;
    }
    ++i; // past '{'
    size_t index = next_auto;
    bool explicit_index = false;
    if (i < fmt.size() && detail::FmtIsDigit(fmt[i])) {
      if (!detail::FmtParseNumber(fmt, i, index))
        return MakeUnexpected(CommonError::InvalidArgument);
      explicit_index = true;
    }
    detail::FormatSpec spec;
    if (i < fmt.size() && fmt[i] == ':') {
      ++i;
      size_t start = i;
      while (i < fmt.size() && fmt[i] != '}')
        ++i;
      if (!detail::FmtParseSpec(StringView(fmt.data() + start, i - start),
                                spec))
        return MakeUnexpected(CommonError::InvalidArgument);
    } else {
      detail::FmtParseSpec(StringView(), spec);
    }
    if (i >= fmt.size() || fmt[i] != '}')
      return MakeUnexpected(CommonError::InvalidArgument);
    ++i;
    if (!explicit_index)
      ++next_auto;
    if (index >= n)
      return MakeUnexpected(CommonError::InvalidArgument);
    Expected<void, CommonError> r = detail::FmtOne(sb, spec, args[index]);
    if (!r)
      return r;
  }
  return {};
}

template <typename... Args>
Expected<void, CommonError> FormatTo(StringBuilder &sb, StringView fmt,
                                     const Args &...args) {
  // The extra element keeps the array non-empty when there are no arguments.
  const FormatArg arr[] = {FormatArg(args)..., FormatArg()};
  return VFormatTo(sb, fmt, arr, sizeof...(Args));
}

// Formats into a new String using the given allocator.
template <typename... Args>
VAPOR_NODISCARD Expected<String, CommonError>
FormatIn(Allocator &alloc, StringView fmt, const Args &...args) {
  StringBuilder sb(alloc);
  Expected<void, CommonError> r = FormatTo(sb, fmt, args...);
  if (!r)
    return MakeUnexpected(r.error());
  return sb.Take();
}

// Formats into a new String using the default allocator.
template <typename... Args>
VAPOR_NODISCARD Expected<String, CommonError> Format(StringView fmt,
                                                     const Args &...args) {
  return FormatIn(DefaultAllocator::GetDefault(), fmt, args...);
}

} // namespace vapor
