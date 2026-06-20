#pragma once
#include <assert.h>
#include <stddef.h>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>

#if defined(VAPOR_USE_STL) && (__cplusplus >= 202002L)
#if defined(__has_include)
#if __has_include(<span>)
#include <span>
#define VAPOR_HAS_STD_SPAN 1
#endif
#else
#include <span>
#define VAPOR_HAS_STD_SPAN 1
#endif
#endif

namespace vapor {

enum class SpanError { OutOfBounds, EmptySpan };

template <typename T> class VAPOR_NODISCARD Span {
public:
  typedef T value_type;
  typedef T *pointer;
  typedef const T *const_pointer;
  typedef T &reference;
  typedef const T &const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T *iterator;
  typedef const T *const_iterator;

  static VAPOR_CXX14_CONSTEXPR inline size_type npos = size_type(-1);

private:
  pointer m_data;
  size_type m_length;

public:
  VAPOR_CXX14_CONSTEXPR Span() noexcept : m_data(nullptr), m_length(0) {}
  VAPOR_CXX14_CONSTEXPR Span(pointer ptr, size_type count)
      : m_data(ptr), m_length(count) {}
  VAPOR_CXX14_CONSTEXPR Span(pointer first, pointer last)
      : m_data(first), m_length(last - first) {}

  template <size_type N>
  VAPOR_CXX14_CONSTEXPR Span(T (&arr)[N]) noexcept : m_data(arr), m_length(N) {}

  VAPOR_CXX14_CONSTEXPR Span(const Span &) noexcept = default;
  Span &operator=(const Span &) noexcept = default;

  // --- std::span Interoperability ---
#if defined(VAPOR_HAS_STD_SPAN)
  VAPOR_CXX14_CONSTEXPR Span(std::span<T> s) noexcept
      : m_data(s.data()), m_length(s.size()) {}

  Span &operator=(std::span<T> s) noexcept {
    m_data = s.data();
    m_length = s.size();
    return *this;
  }

  VAPOR_CXX14_CONSTEXPR operator std::span<T>() const noexcept {
    return std::span<T>(m_data, m_length);
  }
#endif

  // --- Iterators ---
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR iterator begin() const noexcept {
    return m_data;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR iterator end() const noexcept {
    return m_data + m_length;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_iterator cbegin() const noexcept {
    return m_data;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR const_iterator cend() const noexcept {
    return m_data + m_length;
  }

  // --- Size & Capacity ---
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR size_type size() const noexcept {
    return m_length;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR size_type length() const noexcept {
    return m_length;
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR size_type size_bytes() const noexcept {
    return m_length * sizeof(T);
  }
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR bool empty() const noexcept {
    return m_length == 0;
  }

  // --- Element Access ---
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR reference
  operator[](size_type pos) const {
    assert(pos < m_length);
    return m_data[pos];
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR reference at(size_type pos) const {
    if (pos >= m_length) {
      // Emulating abort logic from your StringView
      volatile int *p = nullptr;
      (void)*p;
    }
    return m_data[pos];
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR reference front() const {
    assert(!empty());
    return *m_data;
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR reference back() const {
    assert(!empty());
    return *(m_data + m_length - 1);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR pointer data() const noexcept {
    return m_data;
  }

  // --- Subviews ---
  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR Span
  subspan(size_type pos, size_type count = npos) const {
    assert(pos <= m_length);
    size_type rcount = (count < m_length - pos) ? count : (m_length - pos);
    return Span(m_data + pos, rcount);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR Span first(size_type count) const {
    assert(count <= m_length);
    return Span(m_data, count);
  }

  VAPOR_NODISCARD VAPOR_CXX14_CONSTEXPR Span last(size_type count) const {
    assert(count <= m_length);
    return Span(m_data + (m_length - count), count);
  }

  // --- Modification Helpers ---
  VAPOR_CXX14_CONSTEXPR void remove_prefix(size_type n) {
    assert(n <= m_length);
    m_data += n;
    m_length -= n;
  }

  VAPOR_CXX14_CONSTEXPR void remove_suffix(size_type n) {
    assert(n <= m_length);
    m_length -= n;
  }

  // --- Comparison Operators ---
  VAPOR_NODISCARD bool operator==(const Span &rhs) const noexcept {
    if (m_length != rhs.m_length)
      return false;
    if (m_data == rhs.m_data)
      return true;
    for (size_type i = 0; i < m_length; ++i) {
      if (m_data[i] != rhs.m_data[i])
        return false;
    }
    return true;
  }

  VAPOR_NODISCARD bool operator!=(const Span &rhs) const noexcept {
    return !(*this == rhs);
  }

  // --- Checked Element Access ---

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<ReferenceWrapper<T>, SpanError>
  checked_at(size_type pos) const noexcept {
    if (pos >= m_length) {
      return MakeUnexpected(SpanError::OutOfBounds);
    }
    return ReferenceWrapper<T>(m_data[pos]);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<ReferenceWrapper<T>, SpanError>
  checked_front() const noexcept {
    if (empty()) {
      return MakeUnexpected(SpanError::EmptySpan);
    }
    return ReferenceWrapper<T>(*m_data);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<ReferenceWrapper<T>, SpanError>
  checked_back() const noexcept {
    if (empty()) {
      return MakeUnexpected(SpanError::EmptySpan);
    }
    return ReferenceWrapper<T>(*(m_data + m_length - 1));
  }

  // --- Checked Subviews ---

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<Span, SpanError>
  checked_subspan(size_type pos, size_type count = npos) const noexcept {
    if (pos > m_length) {
      return MakeUnexpected(SpanError::OutOfBounds);
    }

    size_type rcount = (count < m_length - pos) ? count : (m_length - pos);
    return Span(m_data + pos, rcount);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<Span, SpanError>
  checked_first(size_type count) const noexcept {
    if (count > m_length) {
      return MakeUnexpected(SpanError::OutOfBounds);
    }
    return Span(m_data, count);
  }

  VAPOR_NODISCARD VAPOR_CXX20_CONSTEXPR Expected<Span, SpanError>
  checked_last(size_type count) const noexcept {
    if (count > m_length) {
      return MakeUnexpected(SpanError::OutOfBounds);
    }
    return Span(m_data + (m_length - count), count);
  }
};

#if __cplusplus >= 201703L

// Deduce type from a raw pointer and size count
template <typename T> Span(T *ptr, size_t count) -> Span<T>;

// Deduce type from an iterator range (first, last)
template <typename T> Span(T *first, T *last) -> Span<T>;

// Deduce type and size automatically from a native C-style array reference
template <typename T, size_t N> Span(T (&arr)[N]) -> Span<T>;

#endif

} // namespace vapor

#if defined(VAPOR_USE_STL)
#include <functional>
#include <string_view>

namespace std {
template <typename T> struct hash<vapor::Span<T>> {
  VAPOR_NODISCARD size_t operator()(const vapor::Span<T> &s) const noexcept {
#if __cplusplus >= 201703L
    // If T is a trivially copyable type (like int, float, char),
    // we can treat the entire span as a raw string of bytes and recycle
    // std::string_view's hash!
    if (std::is_trivially_copyable<T>::value) {
      return std::hash<std::string_view>{}(std::string_view(
          reinterpret_cast<const char *>(s.data()), s.size_bytes()));
    }
#endif
    // Fallback element-by-element combination for complex structs or C++11/14
    size_t seed = s.size();
    std::hash<T> hasher;
    for (size_t i = 0; i < s.size(); ++i) {
      seed ^= hasher(s[i]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};
} // namespace std
#endif
