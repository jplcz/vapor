#pragma once
#include <assert.h>
#include <stddef.h>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>

#if defined(VAPOR_USE_STL)
#include <string>
#if (__cplusplus >= 201703L)
#include <string_view>
#endif
#endif

namespace vapor {

enum class StringViewError { OutOfBounds, EmptyView, ValueNotFound };

struct VAPOR_NODISCARD StringView {
  typedef char value_type;
  typedef char *pointer;
  typedef const char *const_pointer;
  typedef char &reference;
  typedef const char &const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef const char *const_iterator;
  static VAPOR_CXX14_CONSTEXPR size_type npos = size_type(-1);

  const char *m_string;
  size_t m_length;

  // Internal helper for non-STL VAPOR_CXX14_CONSTEXPR-safe length checks
  VAPOR_NODISCARD static VAPOR_CXX14_CONSTEXPR size_t
  CustomStrlen(const char *str) noexcept {
    if (!str)
      return 0;
    size_t len = 0;
    while (str[len] != '\0') {
      ++len;
    }
    return len;
  }

  VAPOR_CXX14_CONSTEXPR StringView() noexcept
      : m_string(nullptr), m_length(0) {}

  VAPOR_CXX14_CONSTEXPR StringView(const StringView &) noexcept = default;
  StringView &operator=(const StringView &) noexcept = default;

  VAPOR_CXX14_CONSTEXPR StringView(const char *str)
      : m_string(str), m_length(CustomStrlen(str)) {}

  VAPOR_CXX14_CONSTEXPR StringView(const char *str, size_type len)
      : m_string(str), m_length(len) {}

#if (__cplusplus >= 201703L) && defined(VAPOR_USE_STL)
  VAPOR_CXX14_CONSTEXPR StringView(const std::string_view &s) noexcept
      : m_string(s.data()), m_length(s.size()) {}

  StringView &operator=(const std::string_view &s) noexcept {
    m_string = s.data();
    m_length = s.size();
    return *this;
  }
#endif

#if (__cplusplus >= 201103L) && defined(VAPOR_USE_STL)
  template <typename Allocator>
  VAPOR_CXX14_CONSTEXPR
  StringView(const std::basic_string<char, std::char_traits<char>, Allocator>
                 &s) noexcept
      : m_string(s.data()), m_length(s.size()) {}

  template <typename Allocator>
  StringView &operator=(const std::basic_string<char, std::char_traits<char>,
                                                Allocator> &s) noexcept {
    m_string = s.data();
    m_length = s.size();
    return *this;
  }

  template <typename Traits = std::char_traits<char>,
            typename Allocator = std::allocator<char>>
  VAPOR_NODISCARD explicit
  operator std::basic_string<char, Traits, Allocator>() const {
    return std::basic_string<char, Traits, Allocator>(data(), size());
  }
#endif

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_iterator begin() const noexcept {
    return m_string;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_iterator end() const noexcept {
    return m_string + m_length;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_iterator cbegin() const noexcept {
    return m_string;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_iterator cend() const noexcept {
    return m_string + m_length;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR size_type size() const noexcept {
    return m_length;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR size_type length() const noexcept {
    return m_length;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR size_type max_size() const noexcept {
    return SIZE_T_MAX;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool empty() const noexcept {
    return m_length == 0;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_reference
  operator[](size_type pos) const {
    assert(pos < m_length);
    return m_string[pos];
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_reference
  at(size_type pos) const {
    if (pos >= m_length)
      abort();
    return m_string[pos];
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_reference front() const {
    assert(!empty());
    return *m_string;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_reference back() const {
    assert(!empty());
    return *(m_string + m_length - 1);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_pointer data() const noexcept {
    return m_string;
  }

  VAPOR_CXX14_CONSTEXPR void remove_prefix(size_type n) {
    assert(n <= m_length);
    m_string += n;
    m_length -= n;
  }

  VAPOR_CXX14_CONSTEXPR void remove_suffix(size_type n) {
    assert(n <= m_length);
    m_length -= n;
  }

  VAPOR_NODISCARD bool operator==(const StringView &rhs) const noexcept {
    return size() == rhs.size() && memcmp(data(), rhs.data(), size()) == 0;
  }

  VAPOR_NODISCARD bool operator!=(const StringView &rhs) const noexcept {
    return !(*this == rhs);
  }

  VAPOR_NODISCARD bool operator<(const StringView &rhs) const noexcept {
    size_t min_len = Min(size(), rhs.size());
    int cmp = memcmp(data(), rhs.data(), min_len);

    if (cmp != 0) {
      return cmp < 0;
    }

    return size() <
           rhs.size(); // If prefixes match, the shorter string is "less"
  }

  VAPOR_NODISCARD bool operator>(const StringView &rhs) const noexcept {
    return rhs < *this;
  }

  VAPOR_NODISCARD bool operator<=(const StringView &rhs) const noexcept {
    return !(*this > rhs);
  }

  VAPOR_NODISCARD bool operator>=(const StringView &rhs) const noexcept {
    return !(*this < rhs);
  }

  VAPOR_CXX14_CONSTEXPR void swap(StringView &other) noexcept {
    const char *tmp_str = m_string;
    m_string = other.m_string;
    other.m_string = tmp_str;

    size_t tmp_len = m_length;
    m_length = other.m_length;
    other.m_length = tmp_len;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool
  starts_with(StringView sv) const noexcept {
    return m_length >= sv.m_length &&
           memcmp(m_string, sv.m_string, sv.m_length) == 0;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool
  starts_with(char c) const noexcept {
    return !empty() && front() == c;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool
  ends_with(StringView sv) const noexcept {
    return m_length >= sv.m_length &&
           memcmp(m_string + (m_length - sv.m_length), sv.m_string,
                  sv.m_length) == 0;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool ends_with(char c) const noexcept {
    return !empty() && back() == c;
  }

  // --- Checked Character Queries ---

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<size_type, StringViewError>
  checked_find(char c, size_type pos = 0) const noexcept {
    if (pos >= m_length) {
      return MakeUnexpected(StringViewError::OutOfBounds);
    }
    for (size_type i = pos; i < m_length; ++i) {
      if (m_string[i] == c)
        return i;
    }
    return MakeUnexpected(StringViewError::ValueNotFound);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<size_type, StringViewError>
  checked_find(StringView sv, size_type pos = 0) const noexcept {
    if (pos + sv.size() > m_length) {
      return MakeUnexpected(StringViewError::OutOfBounds);
    }
    if (sv.empty())
      return pos;

    for (size_type i = pos; i <= m_length - sv.size(); ++i) {
      bool match = true;
      for (size_type j = 0; j < sv.size(); ++j) {
        if (m_string[i + j] != sv[j]) {
          match = false;
          break;
        }
      }
      if (match)
        return i;
    }
    return MakeUnexpected(StringViewError::ValueNotFound);
  }

  // --- Checked Element Access ---

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR
      Expected<ReferenceWrapper<const char>, StringViewError>
      checked_at(size_type pos) const noexcept {
    if (pos >= m_length) {
      return MakeUnexpected(StringViewError::OutOfBounds);
    }
    // Automatically wraps the reference seamlessly
    return ReferenceWrapper<const char>(m_string[pos]);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR
      Expected<ReferenceWrapper<const char>, StringViewError>
      checked_front() const noexcept {
    if (empty()) {
      return MakeUnexpected(StringViewError::EmptyView);
    }
    return ReferenceWrapper<const char>(*m_string);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR
      Expected<ReferenceWrapper<const char>, StringViewError>
      checked_back() const noexcept {
    if (empty()) {
      return MakeUnexpected(StringViewError::EmptyView);
    }
    return ReferenceWrapper<const char>(*(m_string + m_length - 1));
  }

  // --- Checked Trimming & Slicing ---

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<StringView, StringViewError>
  checked_remove_prefix(size_type n) const noexcept {
    if (n > m_length) {
      return MakeUnexpected(StringViewError::OutOfBounds);
    }
    return StringView(m_string + n, m_length - n);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<StringView, StringViewError>
  checked_remove_suffix(size_type n) const noexcept {
    if (n > m_length) {
      return MakeUnexpected(StringViewError::OutOfBounds);
    }
    return StringView(m_string, m_length - n);
  }

  // --- Checked Tokenization / Splitting ---

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<StringView, StringViewError>
  checked_split_by(char delimiter) const noexcept {
    if (empty()) {
      return MakeUnexpected(StringViewError::EmptyView);
    }

    for (size_type i = 0; i < m_length; ++i) {
      if (m_string[i] == delimiter) {
        return StringView(m_string, i);
      }
    }
    // If no delimiter is found, return the whole string as the singular token
    return *this;
  }
};
} // namespace vapor

#if defined(VAPOR_USE_STL)
#include <functional>
#include <string>

#if __cplusplus >= 201703L
#include <string_view>
#endif

namespace std {
template <> struct hash<vapor::StringView> {
  VAPOR_NODISCARD size_t
  operator()(const vapor::StringView &sv) const noexcept {
#if __cplusplus >= 201703L
    // C++17+: Recycle the native std::string_view hash directly
    return std::hash<std::string_view>{}(
        std::string_view(sv.data(), sv.size()));
#else
    // C++11/C++14: Fallback to construction allocation hash or simple seed
    // If allocations are absolutely forbidden even in STL mode, use a basic
    // hash_combine. For simplicity, we can reuse std::hash for pointers and
    // sizes:
    size_t h1 = std::hash<const char *>{}(sv.data());
    size_t h2 = std::hash<size_t>{}(sv.size());
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
#endif
  }
};
} // namespace std
#endif
