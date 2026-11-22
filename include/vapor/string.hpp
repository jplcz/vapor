#pragma once
#include <stddef.h>
#include <string.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/string_view.hpp>
#include <vapor/utility.hpp>

#if defined(VAPOR_USE_STL)
#include <string>
#endif

namespace vapor {

class VAPOR_NODISCARD String {
public:
  typedef char value_type;
  typedef char *pointer;
  typedef const char *const_pointer;
  typedef char &reference;
  typedef const char &const_reference;
  typedef size_t size_type;

private:
  Allocator *m_allocator;
  pointer m_data;
  size_type m_length;
  size_type m_capacity;

  // Internal helper to establish initial memory bounds cleanly
  Expected<void, CommonError> AllocateBuffer(size_type cap) noexcept {
    if (cap == 0)
      return {};

    // We always request cap + 1 bytes to guarantee room for a null terminator
    // '\0'
    void *raw = m_allocator->Allocate(cap + 1);
    if (!raw)
      return MakeUnexpected(CommonError::OutOfMemory);

    m_data = static_cast<pointer>(raw);
    m_capacity = cap;
    m_data[0] = '\0';
    return {};
  }

public:
  explicit String(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_allocator(&alloc), m_data(nullptr), m_length(0), m_capacity(0) {}

#if defined(VAPOR_USE_STL)
  template <typename Traits = std::char_traits<value_type>,
            typename Alloc = std::allocator<value_type>>
  VAPOR_NODISCARD explicit
  operator std::basic_string<value_type, Traits, Alloc>() const {
    return std::basic_string<value_type, Traits, Alloc>(data(), size());
  }

  // Creates a vapor::String from a std::string, returning an out-of-memory
  // error if allocation fails
  template <typename Traits, typename Alloc>
  VAPOR_NODISCARD static Expected<String, CommonError>
  FromStdString(const std::basic_string<value_type, Traits, Alloc> &str,
                Allocator &alloc = DefaultAllocator::GetDefault()) noexcept {
    String new_string(alloc);

    auto res = new_string.append(StringView(str.data(), str.size()));
    if (!res) {
      return MakeUnexpected(res.error());
    }

    return new_string;
  }

  // --- Safe Mutators ---

  // In-place assignment from a std::string that returns a validation token
  template <typename Traits, typename Alloc>
  Expected<void, CommonError>
  assign(const std::basic_string<value_type, Traits, Alloc> &str) noexcept {
    clear();
    return append(StringView(str.data(), str.size()));
  }
#endif

  ~String() {
    if (m_data) {
      m_allocator->Deallocate(m_data, m_capacity + 1);
    }
  }

  String(String &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity) {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  String &operator=(String &&other) noexcept {
    if (this != &other) {
      if (m_data)
        m_allocator->Deallocate(m_data, m_capacity + 1);
      m_allocator = other.m_allocator;
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;

      other.m_data = nullptr;
      other.m_length = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  // Prevent implicit copies to keep developers aware of unexpected allocations.
  // Explicit copies should be executed via a dedicated 'clone()' method.
  String(const String &) = delete;
  String &operator=(const String &) = delete;

  // Deep copy factory method that returns an Expected wrapper
  Expected<String, CommonError> Clone() const & noexcept {
    String copy(*m_allocator);
    if (m_length > 0) {
      auto res = copy.reserve(m_capacity);
      if (!res)
        return MakeUnexpected(res.error());

      // Replaced manual character-by-character copy loop
      ::memcpy(copy.m_data, m_data, m_length);

      copy.m_length = m_length;
      copy.m_data[m_length] = '\0';
    }
    return copy;
  }

  VAPOR_NODISCARD Expected<String, CommonError> Clone() && noexcept {
    // The object is a temporary! Instead of deep copying,
    // we can cheaply cast ourselves to a mutable rvalue and move our data out.
    return String(Move(*this));
  }

  Expected<void, CommonError> reserve(size_type new_capacity) noexcept {
    if (new_capacity <= m_capacity)
      return {};

    if (!m_data) {
      return AllocateBuffer(new_capacity);
    }

    void *raw = m_allocator->Reallocate(m_data, new_capacity + 1);
    if (!raw)
      return MakeUnexpected(CommonError::OutOfMemory);

    m_data = static_cast<pointer>(raw);
    m_capacity = new_capacity;
    return {};
  }

  Expected<void, CommonError> push_back(char c) noexcept {
    if (m_length >= m_capacity) {
      size_type new_cap = m_capacity == 0 ? 8 : m_capacity * 2;
      auto res = reserve(new_cap);
      if (!res)
        return MakeUnexpected(res.error());
    }

    m_data[m_length] = c;
    m_length++;
    m_data[m_length] = '\0';
    return {};
  }

  Expected<void, CommonError> append(StringView sv) noexcept {
    if (sv.empty())
      return {};

    if (sv.size() > VAPOR_SIZE_MAX - 1 - m_length)
      return MakeUnexpected(CommonError::OutOfMemory);
    size_type required_capacity = m_length + sv.size();

    // sv may point into our own buffer, which reserve() can move; keep it as
    // an offset so it stays valid.
    bool aliased = m_data && sv.data() >= m_data && sv.data() < m_data + m_length;
    size_type offset = aliased ? static_cast<size_type>(sv.data() - m_data) : 0;

    if (required_capacity > m_capacity) {
      size_type new_cap = m_capacity == 0 ? 8 : m_capacity * 2;
      if (new_cap < required_capacity)
        new_cap = required_capacity;

      auto res = reserve(new_cap);
      if (!res)
        return MakeUnexpected(res.error());
    }

    const char *src = aliased ? m_data + offset : sv.data();
    ::memmove(m_data + m_length, src, sv.size());

    m_length += sv.size();
    m_data[m_length] = '\0';
    return {};
  }

  Expected<void, CommonError> erase(size_type pos, size_type count) noexcept {
    if (pos >= m_length)
      return MakeUnexpected(CommonError::OutOfBounds);
    if (count == 0)
      return {};

    if (pos + count > m_length) {
      count = m_length - pos;
    }

    size_type elements_to_move = m_length - (pos + count);
    if (elements_to_move > 0) {
      ::memmove(m_data + pos, m_data + pos + count, elements_to_move);
    }

    m_length -= count;
    m_data[m_length] = '\0';
    return {};
  }

  void clear() noexcept {
    m_length = 0;
    if (m_data)
      m_data[0] = '\0';
  }

  VAPOR_NODISCARD size_type size() const noexcept { return m_length; }
  VAPOR_NODISCARD size_type capacity() const noexcept { return m_capacity; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_length == 0; }
  VAPOR_NODISCARD const_pointer c_str() const & noexcept {
    return m_data ? m_data : "";
  }
  VAPOR_NODISCARD const_pointer data() const & noexcept {
    return m_data ? m_data : "";
  }
  VAPOR_NODISCARD Expected<pointer, CommonError> try_data() & noexcept {
    if (!m_data || m_length == 0)
      return MakeUnexpected(CommonError::EmptyString);
    return m_data;
  }

  VAPOR_NODISCARD operator StringView() const & noexcept {
    return StringView(data(), size());
  }

  VAPOR_NODISCARD
  VAPOR_CXX20_CONSTEXPR Expected<ReferenceWrapper<char>, CommonError>
  checked_at(size_type pos) & noexcept {
    if (pos >= m_length)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<char>(m_data[pos]);
  }

  VAPOR_NODISCARD
  VAPOR_CXX20_CONSTEXPR Expected<ReferenceWrapper<const char>, CommonError>
  checked_at(size_type pos) const & noexcept {
    if (pos >= m_length)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const char>(m_data[pos]);
  }
};

namespace detail {
// Length-aware three-way compare; unlike strcmp it handles embedded '\0'.
inline int CompareBytes(const char *a, size_t an, const char *b,
                        size_t bn) noexcept {
  size_t n = an < bn ? an : bn;
  int cmp = n ? ::memcmp(a, b, n) : 0;
  if (cmp != 0)
    return cmp;
  return an < bn ? -1 : (an > bn ? 1 : 0);
}
} // namespace detail

VAPOR_NODISCARD inline bool operator==(const String &lhs,
                                       const String &rhs) noexcept {
  return detail::CompareBytes(lhs.data(), lhs.size(), rhs.data(),
                              rhs.size()) == 0;
}

VAPOR_NODISCARD inline bool operator!=(const String &lhs,
                                       const String &rhs) noexcept {
  return !(lhs == rhs);
}

VAPOR_NODISCARD inline bool operator<(const String &lhs,
                                      const String &rhs) noexcept {
  return detail::CompareBytes(lhs.data(), lhs.size(), rhs.data(), rhs.size()) < 0;
}
VAPOR_NODISCARD inline bool operator<=(const String &lhs,
                                       const String &rhs) noexcept {
  return !(rhs < lhs);
}
VAPOR_NODISCARD inline bool operator>(const String &lhs,
                                      const String &rhs) noexcept {
  return rhs < lhs;
}
VAPOR_NODISCARD inline bool operator>=(const String &lhs,
                                       const String &rhs) noexcept {
  return !(lhs < rhs);
}

VAPOR_NODISCARD inline bool operator==(const String &lhs,
                                       StringView rhs) noexcept {
  return detail::CompareBytes(lhs.data(), lhs.size(), rhs.data(),
                              rhs.size()) == 0;
}

VAPOR_NODISCARD inline bool operator==(StringView lhs,
                                       const String &rhs) noexcept {
  return rhs == lhs;
}
VAPOR_NODISCARD inline bool operator!=(const String &lhs,
                                       StringView rhs) noexcept {
  return !(lhs == rhs);
}
VAPOR_NODISCARD inline bool operator!=(StringView lhs,
                                       const String &rhs) noexcept {
  return !(rhs == lhs);
}

VAPOR_NODISCARD inline bool operator<(const String &lhs,
                                      StringView rhs) noexcept {
  return detail::CompareBytes(lhs.data(), lhs.size(), rhs.data(),
                              rhs.size()) < 0;
}

VAPOR_NODISCARD inline bool operator<(StringView lhs,
                                      const String &rhs) noexcept {
  return detail::CompareBytes(lhs.data(), lhs.size(), rhs.data(),
                              rhs.size()) < 0;
}
VAPOR_NODISCARD inline bool operator<=(const String &lhs,
                                       StringView rhs) noexcept {
  return !(rhs < lhs);
}
VAPOR_NODISCARD inline bool operator<=(StringView lhs,
                                       const String &rhs) noexcept {
  return !(rhs < lhs);
}
VAPOR_NODISCARD inline bool operator>(const String &lhs,
                                      StringView rhs) noexcept {
  return rhs < lhs;
}
VAPOR_NODISCARD inline bool operator>(StringView lhs,
                                      const String &rhs) noexcept {
  return rhs < lhs;
}
VAPOR_NODISCARD inline bool operator>=(const String &lhs,
                                       StringView rhs) noexcept {
  return !(lhs < rhs);
}
VAPOR_NODISCARD inline bool operator>=(StringView lhs,
                                       const String &rhs) noexcept {
  return !(lhs < rhs);
}

VAPOR_NODISCARD inline bool operator==(const String &lhs,
                                       const char *rhs) noexcept {
  if (!rhs)
    return false;
  return detail::CompareBytes(lhs.data(), lhs.size(), rhs, ::strlen(rhs)) == 0;
}

VAPOR_NODISCARD inline bool operator==(const char *lhs,
                                       const String &rhs) noexcept {
  return rhs == lhs;
}
VAPOR_NODISCARD inline bool operator!=(const String &lhs,
                                       const char *rhs) noexcept {
  return !(lhs == rhs);
}
VAPOR_NODISCARD inline bool operator!=(const char *lhs,
                                       const String &rhs) noexcept {
  return !(rhs == lhs);
}

VAPOR_NODISCARD inline bool operator<(const String &lhs,
                                      const char *rhs) noexcept {
  if (!rhs)
    return false;
  return detail::CompareBytes(lhs.data(), lhs.size(), rhs, ::strlen(rhs)) < 0;
}

VAPOR_NODISCARD inline bool operator<(const char *lhs,
                                      const String &rhs) noexcept {
  if (!lhs)
    return true;
  return detail::CompareBytes(lhs, ::strlen(lhs), rhs.data(), rhs.size()) < 0;
}

VAPOR_NODISCARD inline bool operator<=(const String &lhs,
                                       const char *rhs) noexcept {
  return !(rhs < lhs);
}
VAPOR_NODISCARD inline bool operator<=(const char *lhs,
                                       const String &rhs) noexcept {
  return !(rhs < lhs);
}
VAPOR_NODISCARD inline bool operator>(const String &lhs,
                                      const char *rhs) noexcept {
  return rhs < lhs;
}
VAPOR_NODISCARD inline bool operator>(const char *lhs,
                                      const String &rhs) noexcept {
  return rhs < lhs;
}
VAPOR_NODISCARD inline bool operator>=(const String &lhs,
                                       const char *rhs) noexcept {
  return !(lhs < rhs);
}
VAPOR_NODISCARD inline bool operator>=(const char *lhs,
                                       const String &rhs) noexcept {
  return !(lhs < rhs);
}

} // namespace vapor

#if defined(VAPOR_USE_STL)
#include <functional>

#if __cplusplus >= 201703L
#include <string_view>
#endif

namespace std {
template <> struct hash<vapor::String> {
  VAPOR_NODISCARD size_t operator()(const vapor::String &str) const noexcept {
#if __cplusplus >= 201703L
    // C++17+: Directly recycle the heavily optimized std::string_view hash
    return std::hash<std::string_view>{}(
        std::string_view(str.data(), str.size()));
#else
    // C++11/C++14: Fallback to a safe identity combination loop if string_view
    // is unavailable
    size_t seed = str.size();
    const char *ptr = str.data();
    for (size_t i = 0; i < str.size(); ++i) {
      seed ^=
          std::hash<char>{}(ptr[i]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
#endif
  }
};
} // namespace std
#endif
