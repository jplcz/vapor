#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/hash.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/string_view.hpp>

// SmallString<N>: a text string of at most N characters stored inside the
// object itself.
//
// Why: String needs an allocator, can fail with OutOfMemory and touches the
// heap. For short, bounded text (a name, a path component, an error message, a
// key in a table) that is wasteful, and impossible in interrupt handlers or
// early boot code where no allocator exists yet. SmallString keeps the
// characters in an array, so it never allocates and copying it cannot fail
// (unlike String, it is copyable).
//
// How: `SmallString<32> name; name.append("kernel"); name.push_back('!');`
// The capacity N is fixed at compile time. Operations that would exceed it
// return Expected<..., CommonError> with CapacityExceeded and leave the string
// unchanged (nothing is truncated silently). The text is always followed by a
// '\0', so c_str() can be passed to C APIs. Converts implicitly to StringView.
// If you do not know a sensible bound, use String instead.

namespace vapor {

template <size_t N> class SmallString {
public:
  typedef char value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef char &reference;
  typedef const char &const_reference;
  typedef char *pointer;
  typedef const char *const_pointer;
  typedef char *iterator;
  typedef const char *const_iterator;

private:
  char m_data[N + 1];
  size_type m_size;

public:
  SmallString() noexcept : m_size(0) { m_data[0] = '\0'; }

  // Builds a string from `sv`, or fails if it does not fit.
  VAPOR_NODISCARD static Expected<SmallString, CommonError>
  From(StringView sv) noexcept {
    SmallString s;
    Expected<void, CommonError> r = s.append(sv);
    if (!r)
      return MakeUnexpected(r.error());
    return s;
  }

  VAPOR_NODISCARD size_type size() const noexcept { return m_size; }
  VAPOR_NODISCARD size_type length() const noexcept { return m_size; }
  VAPOR_NODISCARD static constexpr size_type capacity() noexcept { return N; }
  VAPOR_NODISCARD static constexpr size_type max_size() noexcept { return N; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }

  VAPOR_NODISCARD const_pointer data() const noexcept { return m_data; }
  VAPOR_NODISCARD const_pointer c_str() const noexcept { return m_data; }
  VAPOR_NODISCARD pointer data() noexcept { return m_data; }

  VAPOR_NODISCARD reference operator[](size_type pos) noexcept {
    assert(pos < m_size);
    return m_data[pos];
  }
  VAPOR_NODISCARD const_reference operator[](size_type pos) const noexcept {
    assert(pos < m_size);
    return m_data[pos];
  }
  VAPOR_NODISCARD reference at(size_type pos) noexcept {
    if (pos >= m_size)
      abort();
    return m_data[pos];
  }
  VAPOR_NODISCARD const_reference at(size_type pos) const noexcept {
    if (pos >= m_size)
      abort();
    return m_data[pos];
  }
  VAPOR_NODISCARD reference front() noexcept {
    assert(m_size > 0);
    return m_data[0];
  }
  VAPOR_NODISCARD const_reference front() const noexcept {
    assert(m_size > 0);
    return m_data[0];
  }
  VAPOR_NODISCARD reference back() noexcept {
    assert(m_size > 0);
    return m_data[m_size - 1];
  }
  VAPOR_NODISCARD const_reference back() const noexcept {
    assert(m_size > 0);
    return m_data[m_size - 1];
  }

  // Non-trapping variant of at() (vapor extension).
  VAPOR_NODISCARD Expected<ReferenceWrapper<char>, CommonError>
  checked_at(size_type pos) noexcept {
    if (pos >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<char>(m_data[pos]);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const char>, CommonError>
  checked_at(size_type pos) const noexcept {
    if (pos >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const char>(m_data[pos]);
  }

  VAPOR_NODISCARD iterator begin() noexcept { return m_data; }
  VAPOR_NODISCARD const_iterator begin() const noexcept { return m_data; }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return m_data; }
  VAPOR_NODISCARD iterator end() noexcept { return m_data + m_size; }
  VAPOR_NODISCARD const_iterator end() const noexcept {
    return m_data + m_size;
  }
  VAPOR_NODISCARD const_iterator cend() const noexcept {
    return m_data + m_size;
  }

  VAPOR_NODISCARD operator StringView() const noexcept {
    return StringView(m_data, m_size);
  }
  VAPOR_NODISCARD StringView view() const noexcept {
    return StringView(m_data, m_size);
  }

  void clear() noexcept {
    m_size = 0;
    m_data[0] = '\0';
  }

  Expected<void, CommonError> push_back(char c) noexcept {
    if (m_size == N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    m_data[m_size++] = c;
    m_data[m_size] = '\0';
    return {};
  }

  void pop_back() noexcept {
    assert(m_size > 0);
    m_data[--m_size] = '\0';
  }

  // `sv` may point into this string.
  Expected<void, CommonError> append(StringView sv) noexcept {
    if (sv.size() > N - m_size)
      return MakeUnexpected(CommonError::CapacityExceeded);
    if (sv.size() != 0)
      ::memmove(m_data + m_size, sv.data(), sv.size());
    m_size += sv.size();
    m_data[m_size] = '\0';
    return {};
  }

  Expected<void, CommonError> assign(StringView sv) noexcept {
    if (sv.size() > N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    if (sv.size() != 0)
      ::memmove(m_data, sv.data(), sv.size());
    m_size = sv.size();
    m_data[m_size] = '\0';
    return {};
  }

  // Removes up to `count` characters starting at `pos`.
  Expected<void, CommonError> erase(size_type pos, size_type count) noexcept {
    if (pos > m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    if (count > m_size - pos)
      count = m_size - pos;
    ::memmove(m_data + pos, m_data + pos + count, m_size - pos - count);
    m_size -= count;
    m_data[m_size] = '\0';
    return {};
  }

  // Inserts `sv` before `pos`. `sv` must not point into this string.
  Expected<void, CommonError> insert(size_type pos, StringView sv) noexcept {
    if (pos > m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    if (sv.size() > N - m_size)
      return MakeUnexpected(CommonError::CapacityExceeded);
    ::memmove(m_data + pos + sv.size(), m_data + pos, m_size - pos);
    if (sv.size() != 0)
      ::memcpy(m_data + pos, sv.data(), sv.size());
    m_size += sv.size();
    m_data[m_size] = '\0';
    return {};
  }

  // Shrinks, or grows by filling with `fill`.
  Expected<void, CommonError> resize(size_type n, char fill = '\0') noexcept {
    if (n > N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    for (size_type i = m_size; i < n; ++i)
      m_data[i] = fill;
    m_size = n;
    m_data[m_size] = '\0';
    return {};
  }

  void swap(SmallString &other) noexcept {
    SmallString tmp(*this);
    *this = other;
    other = tmp;
  }
};

namespace detail {
VAPOR_NODISCARD inline int CompareText(StringView a, StringView b) noexcept {
  size_t n = a.size() < b.size() ? a.size() : b.size();
  int c = n ? ::memcmp(a.data(), b.data(), n) : 0;
  if (c != 0)
    return c;
  return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
}
} // namespace detail

template <size_t N, size_t M>
VAPOR_NODISCARD bool operator==(const SmallString<N> &a,
                                const SmallString<M> &b) noexcept {
  return detail::CompareText(a.view(), b.view()) == 0;
}
template <size_t N, size_t M>
VAPOR_NODISCARD bool operator!=(const SmallString<N> &a,
                                const SmallString<M> &b) noexcept {
  return !(a == b);
}
template <size_t N, size_t M>
VAPOR_NODISCARD bool operator<(const SmallString<N> &a,
                               const SmallString<M> &b) noexcept {
  return detail::CompareText(a.view(), b.view()) < 0;
}
template <size_t N, size_t M>
VAPOR_NODISCARD bool operator>(const SmallString<N> &a,
                               const SmallString<M> &b) noexcept {
  return b < a;
}
template <size_t N, size_t M>
VAPOR_NODISCARD bool operator<=(const SmallString<N> &a,
                                const SmallString<M> &b) noexcept {
  return !(b < a);
}
template <size_t N, size_t M>
VAPOR_NODISCARD bool operator>=(const SmallString<N> &a,
                                const SmallString<M> &b) noexcept {
  return !(a < b);
}

template <size_t N>
VAPOR_NODISCARD bool operator==(const SmallString<N> &a,
                                StringView b) noexcept {
  return detail::CompareText(a.view(), b) == 0;
}
template <size_t N>
VAPOR_NODISCARD bool operator==(StringView a,
                                const SmallString<N> &b) noexcept {
  return b == a;
}
template <size_t N>
VAPOR_NODISCARD bool operator!=(const SmallString<N> &a,
                                StringView b) noexcept {
  return !(a == b);
}
template <size_t N>
VAPOR_NODISCARD bool operator!=(StringView a,
                                const SmallString<N> &b) noexcept {
  return !(b == a);
}
template <size_t N>
VAPOR_NODISCARD bool operator==(const SmallString<N> &a,
                                const char *b) noexcept {
  return a == StringView(b);
}
template <size_t N>
VAPOR_NODISCARD bool operator!=(const SmallString<N> &a,
                                const char *b) noexcept {
  return !(a == StringView(b));
}

// Same hash as the String and StringView with identical text.
template <size_t N> struct Hash<SmallString<N>, void> {
  VAPOR_NODISCARD size_t operator()(const SmallString<N> &s) const noexcept {
    return HashBytes(s.data(), s.size());
  }
};

} // namespace vapor
