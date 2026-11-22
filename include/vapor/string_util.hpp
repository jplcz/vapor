#pragma once
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>
#include <vapor/string_view.hpp>
#include <vapor/type_traits.hpp>

// Text helpers for StringView: trimming, splitting, searching and parsing
// numbers.
//
// Why: nearly every program reads text (config files, command lines, protocol
// messages) and ends up hand-writing the same loops to cut it into fields and
// turn "42" into 42. C's atoi()/strtol() silently accept garbage and need a
// NUL-terminated string; these helpers work on a StringView (pointer + length,
// no copy, no allocation), check the whole input and report failures through
// Expected instead of returning 0.
//
// How:
//
//   Trim("  hi \n")                         -> "hi"
//   for (StringView part : Split("a,b,,c", ',')) ...   -> "a" "b" "" "c"
//   ParseInt<int>("-42")                    -> Expected<int, CommonError>
//   ParseInt<uint32_t>("ff", 16)            -> 255
//
// All returned StringViews point into the original text, so they are only
// valid while that text is alive and unchanged.
//
// Parsing is strict: no surrounding whitespace, the whole text must be
// consumed. Errors: InvalidArgument (empty, bad character, bad base) and
// ValueOutOfRange (does not fit the requested type).

namespace vapor {

// --------------------------------------------------------------- characters
// ASCII-only on purpose: the result never depends on the current locale.

VAPOR_NODISCARD constexpr bool IsAsciiSpace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}
VAPOR_NODISCARD constexpr bool IsAsciiDigit(char c) noexcept {
  return c >= '0' && c <= '9';
}
VAPOR_NODISCARD constexpr bool IsAsciiAlpha(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
VAPOR_NODISCARD constexpr char AsciiToLower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
VAPOR_NODISCARD constexpr char AsciiToUpper(char c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// True when both texts are equal when ASCII letters are compared without case.
VAPOR_NODISCARD inline bool EqualsIgnoreCase(StringView a,
                                             StringView b) noexcept {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (AsciiToLower(a[i]) != AsciiToLower(b[i]))
      return false;
  return true;
}

// ----------------------------------------------------------------- trimming

VAPOR_NODISCARD inline StringView TrimLeft(StringView sv) noexcept {
  size_t i = 0;
  while (i < sv.size() && IsAsciiSpace(sv[i]))
    ++i;
  return StringView(sv.data() + i, sv.size() - i);
}
VAPOR_NODISCARD inline StringView TrimRight(StringView sv) noexcept {
  size_t n = sv.size();
  while (n > 0 && IsAsciiSpace(sv[n - 1]))
    --n;
  return StringView(sv.data(), n);
}
// Removes leading and trailing ASCII whitespace.
VAPOR_NODISCARD inline StringView Trim(StringView sv) noexcept {
  return TrimRight(TrimLeft(sv));
}

// If `sv` starts with `prefix`, removes it and returns true; else leaves `sv`
// alone and returns false. Handy for parsing "--flag=value" style text.
inline bool ConsumePrefix(StringView &sv, StringView prefix) noexcept {
  if (!sv.starts_with(prefix))
    return false;
  sv = StringView(sv.data() + prefix.size(), sv.size() - prefix.size());
  return true;
}
inline bool ConsumeSuffix(StringView &sv, StringView suffix) noexcept {
  if (!sv.ends_with(suffix))
    return false;
  sv = StringView(sv.data(), sv.size() - suffix.size());
  return true;
}

// ---------------------------------------------------------------- searching

// Index of the first occurrence at or after `pos`, or StringView::npos.
// Unlike StringView::checked_find this never reports an error: "not found" is
// an ordinary outcome here.
VAPOR_NODISCARD inline size_t Find(StringView text, StringView needle,
                                   size_t pos = 0) noexcept {
  if (pos > text.size() || needle.size() > text.size() - pos)
    return StringView::npos;
  if (needle.empty())
    return pos;
  const size_t last = text.size() - needle.size();
  for (size_t i = pos; i <= last; ++i)
    if (::memcmp(text.data() + i, needle.data(), needle.size()) == 0)
      return i;
  return StringView::npos;
}
VAPOR_NODISCARD inline size_t Find(StringView text, char c,
                                   size_t pos = 0) noexcept {
  for (size_t i = pos; i < text.size(); ++i)
    if (text[i] == c)
      return i;
  return StringView::npos;
}
VAPOR_NODISCARD inline bool Contains(StringView text,
                                     StringView needle) noexcept {
  return Find(text, needle) != StringView::npos;
}
VAPOR_NODISCARD inline bool Contains(StringView text, char c) noexcept {
  return Find(text, c) != StringView::npos;
}

// ---------------------------------------------------------------- splitting

namespace detail {
struct Splitter {
  StringView text;
  StringView delim;
  char ch;
  bool single;

  // Start of the next delimiter at or after pos, or text.size().
  VAPOR_NODISCARD size_t Next(size_t pos) const noexcept {
    if (!single && delim.empty())
      return text.size();
    size_t i = single ? Find(text, ch, pos) : Find(text, delim, pos);
    return i == StringView::npos ? text.size() : i;
  }
  VAPOR_NODISCARD size_t DelimSize() const noexcept {
    return single ? 1 : delim.size();
  }
};
} // namespace detail

// Range over the pieces of a text, created by Split(). Pieces are produced
// lazily; nothing is allocated or copied.
class SplitView {
  detail::Splitter m_s;

public:
  class iterator {
    friend class SplitView;
    detail::Splitter m_s;
    size_t m_pos;
    size_t m_end;
    bool m_done;

    iterator(const detail::Splitter &s, bool done)
        : m_s(s), m_pos(0), m_end(0), m_done(done) {
      if (!done)
        m_end = m_s.Next(0);
    }

  public:
    typedef StringView value_type;
    typedef ptrdiff_t difference_type;
    typedef const StringView *pointer;
    typedef StringView reference;

    VAPOR_NODISCARD StringView operator*() const noexcept {
      return StringView(m_s.text.data() + m_pos, m_end - m_pos);
    }
    iterator &operator++() noexcept {
      if (m_end >= m_s.text.size()) {
        m_done = true;
      } else {
        m_pos = m_end + m_s.DelimSize();
        m_end = m_s.Next(m_pos);
      }
      return *this;
    }
    iterator operator++(int) noexcept {
      iterator tmp(*this);
      ++*this;
      return tmp;
    }
    VAPOR_NODISCARD bool operator==(const iterator &o) const noexcept {
      return m_done == o.m_done && (m_done || m_pos == o.m_pos);
    }
    VAPOR_NODISCARD bool operator!=(const iterator &o) const noexcept {
      return !(*this == o);
    }
  };

  explicit SplitView(const detail::Splitter &s) noexcept : m_s(s) {}

  VAPOR_NODISCARD iterator begin() const noexcept { return iterator(m_s, false); }
  VAPOR_NODISCARD iterator end() const noexcept { return iterator(m_s, true); }

  // Number of pieces (one more than the number of delimiters found).
  VAPOR_NODISCARD size_t count() const noexcept {
    size_t n = 0;
    for (iterator it = begin(), e = end(); it != e; ++it)
      ++n;
    return n;
  }
};

// Splits at every `delim`. Empty pieces are kept: Split("a,,b", ',') gives
// "a", "", "b", and Split("", ',') gives one empty piece. Use
// `if (piece.empty()) continue;` to skip them.
VAPOR_NODISCARD inline SplitView Split(StringView text, char delim) noexcept {
  detail::Splitter s;
  s.text = text;
  s.ch = delim;
  s.single = true;
  return SplitView(s);
}
// Splits at every occurrence of `delim`. An empty delimiter yields the whole
// text as a single piece.
VAPOR_NODISCARD inline SplitView Split(StringView text,
                                       StringView delim) noexcept {
  detail::Splitter s;
  s.text = text;
  s.delim = delim;
  s.ch = '\0';
  s.single = false;
  return SplitView(s);
}
VAPOR_NODISCARD inline SplitView Split(StringView text,
                                       const char *delim) noexcept {
  return Split(text, StringView(delim));
}

// Cuts `text` at the first `delim`: {before, after}. ValueNotFound if absent.
// Typical use: "key=value".
VAPOR_NODISCARD inline Expected<Pair<StringView, StringView>, CommonError>
SplitOnce(StringView text, StringView delim) noexcept {
  size_t i = Find(text, delim);
  if (delim.empty() || i == StringView::npos)
    return MakeUnexpected(CommonError::ValueNotFound);
  return Pair<StringView, StringView>(
      StringView(text.data(), i),
      StringView(text.data() + i + delim.size(),
                 text.size() - i - delim.size()));
}
VAPOR_NODISCARD inline Expected<Pair<StringView, StringView>, CommonError>
SplitOnce(StringView text, char delim) noexcept {
  size_t i = Find(text, delim);
  if (i == StringView::npos)
    return MakeUnexpected(CommonError::ValueNotFound);
  return Pair<StringView, StringView>(
      StringView(text.data(), i),
      StringView(text.data() + i + 1, text.size() - i - 1));
}

// ------------------------------------------------------------------ parsing

namespace detail {
VAPOR_NODISCARD constexpr int DigitValue(char c) noexcept {
  return (c >= '0' && c <= '9')   ? c - '0'
         : (c >= 'a' && c <= 'z') ? c - 'a' + 10
         : (c >= 'A' && c <= 'Z') ? c - 'A' + 10
                                  : 99;
}
} // namespace detail

// Parses a whole integer. `base` is 2..36, or 0 to detect it from a prefix:
// "0x" hexadecimal, "0b" binary, a leading "0" octal, otherwise decimal. In
// base 16 and 2 the matching prefix is also accepted. A leading '+' is
// allowed, a leading '-' only for signed types.
template <typename T>
VAPOR_NODISCARD Expected<T, CommonError> ParseInt(StringView sv,
                                                  int base = 10) noexcept {
  static_assert(IsIntegral<T>::value && !IsSame<RemoveCVT<T>, bool>::value,
                "ParseInt needs an integer type");
  size_t i = 0;
  bool negative = false;
  if (sv.empty())
    return MakeUnexpected(CommonError::InvalidArgument);
  if (sv[0] == '-') {
    if (!IsSigned<T>::value)
      return MakeUnexpected(CommonError::InvalidArgument);
    negative = true;
    i = 1;
  } else if (sv[0] == '+') {
    i = 1;
  }
  const size_t rest = sv.size() - i;
  if ((base == 0 || base == 16) && rest > 2 && sv[i] == '0' &&
      (sv[i + 1] == 'x' || sv[i + 1] == 'X')) {
    base = 16;
    i += 2;
  } else if ((base == 0 || base == 2) && rest > 2 && sv[i] == '0' &&
             (sv[i + 1] == 'b' || sv[i + 1] == 'B')) {
    base = 2;
    i += 2;
  } else if (base == 0) {
    base = (rest > 1 && sv[i] == '0') ? 8 : 10;
  }
  if (base < 2 || base > 36 || i >= sv.size())
    return MakeUnexpected(CommonError::InvalidArgument);

  const uint64_t umax = ~static_cast<uint64_t>(0) >> (64 - 8 * sizeof(T));
  const uint64_t limit =
      IsSigned<T>::value ? (negative ? (umax >> 1) + 1 : (umax >> 1)) : umax;
  const uint64_t ubase = static_cast<uint64_t>(base);

  uint64_t acc = 0;
  bool overflow = false;
  for (; i < sv.size(); ++i) {
    int d = detail::DigitValue(sv[i]);
    if (d >= base)
      return MakeUnexpected(CommonError::InvalidArgument);
    uint64_t ud = static_cast<uint64_t>(d);
    if (overflow || ud > limit || acc > (limit - ud) / ubase)
      overflow = true;
    else
      acc = acc * ubase + ud;
  }
  if (overflow)
    return MakeUnexpected(CommonError::ValueOutOfRange);
  return static_cast<T>(negative ? 0 - acc : acc);
}

// "true"/"false" (any ASCII case) and "1"/"0".
VAPOR_NODISCARD inline Expected<bool, CommonError>
ParseBool(StringView sv) noexcept {
  if (EqualsIgnoreCase(sv, "true") || sv == StringView("1"))
    return true;
  if (EqualsIgnoreCase(sv, "false") || sv == StringView("0"))
    return false;
  return MakeUnexpected(CommonError::InvalidArgument);
}

// Parses a whole floating-point number ("3.14", "-1e9", "inf", "nan").
// Uses the C library's strtod, so the decimal separator follows the C locale
// (the default "C" locale uses '.'); text longer than 127 characters is
// rejected.
VAPOR_NODISCARD inline Expected<double, CommonError>
ParseDouble(StringView sv) noexcept {
  char buf[128];
  if (sv.empty() || sv.size() >= sizeof(buf) || IsAsciiSpace(sv[0]))
    return MakeUnexpected(CommonError::InvalidArgument);
  ::memcpy(buf, sv.data(), sv.size());
  buf[sv.size()] = '\0';
  char *end = nullptr;
  errno = 0;
  double v = ::strtod(buf, &end);
  if (end != buf + sv.size())
    return MakeUnexpected(CommonError::InvalidArgument);
  if (errno == ERANGE && (v > 1.0 || v < -1.0))
    return MakeUnexpected(CommonError::ValueOutOfRange);
  return v;
}

} // namespace vapor
