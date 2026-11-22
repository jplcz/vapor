#pragma once
#include <stddef.h>
#include <vapor/type_traits.hpp>

namespace vapor {
namespace detail {

// Random-access iterator over any container offering `owner[i]` for
// i in [0, size). Used by Deque and RingBuffer, whose elements are not
// contiguous in memory. It stores the owner and a logical position, so any
// push or pop on the owner must be treated as invalidating it, as with
// std::deque.
template <typename Owner, typename V> class IndexIterator {
  template <typename O, typename W> friend class IndexIterator;
  Owner *m_owner;
  ptrdiff_t m_pos;

public:
  typedef V value_type;
  typedef ptrdiff_t difference_type;
  typedef V *pointer;
  typedef V &reference;

  IndexIterator() noexcept : m_owner(nullptr), m_pos(0) {}
  IndexIterator(Owner *owner, size_t pos) noexcept
      : m_owner(owner), m_pos(static_cast<ptrdiff_t>(pos)) {}
  // iterator -> const_iterator
  template <typename W, typename = EnableIfT<IsSame<const W, V>::value &&
                                             !IsSame<W, V>::value>>
  IndexIterator(
      const IndexIterator<typename RemoveConst<Owner>::type, W> &o) noexcept
      : m_owner(o.m_owner), m_pos(o.m_pos) {}

  V &operator*() const noexcept {
    return (*m_owner)[static_cast<size_t>(m_pos)];
  }
  V *operator->() const noexcept {
    return &(*m_owner)[static_cast<size_t>(m_pos)];
  }
  V &operator[](ptrdiff_t n) const noexcept {
    return (*m_owner)[static_cast<size_t>(m_pos + n)];
  }
  IndexIterator &operator++() noexcept {
    ++m_pos;
    return *this;
  }
  IndexIterator operator++(int) noexcept {
    IndexIterator t(*this);
    ++m_pos;
    return t;
  }
  IndexIterator &operator--() noexcept {
    --m_pos;
    return *this;
  }
  IndexIterator operator--(int) noexcept {
    IndexIterator t(*this);
    --m_pos;
    return t;
  }
  IndexIterator &operator+=(ptrdiff_t n) noexcept {
    m_pos += n;
    return *this;
  }
  IndexIterator &operator-=(ptrdiff_t n) noexcept {
    m_pos -= n;
    return *this;
  }
  friend IndexIterator operator+(IndexIterator it, ptrdiff_t n) noexcept {
    it += n;
    return it;
  }
  friend IndexIterator operator+(ptrdiff_t n, IndexIterator it) noexcept {
    it += n;
    return it;
  }
  friend IndexIterator operator-(IndexIterator it, ptrdiff_t n) noexcept {
    it -= n;
    return it;
  }
  friend ptrdiff_t operator-(const IndexIterator &a,
                             const IndexIterator &b) noexcept {
    return a.m_pos - b.m_pos;
  }
  friend bool operator==(const IndexIterator &a,
                         const IndexIterator &b) noexcept {
    return a.m_pos == b.m_pos;
  }
  friend bool operator!=(const IndexIterator &a,
                         const IndexIterator &b) noexcept {
    return a.m_pos != b.m_pos;
  }
  friend bool operator<(const IndexIterator &a,
                        const IndexIterator &b) noexcept {
    return a.m_pos < b.m_pos;
  }
  friend bool operator>(const IndexIterator &a,
                        const IndexIterator &b) noexcept {
    return a.m_pos > b.m_pos;
  }
  friend bool operator<=(const IndexIterator &a,
                         const IndexIterator &b) noexcept {
    return a.m_pos <= b.m_pos;
  }
  friend bool operator>=(const IndexIterator &a,
                         const IndexIterator &b) noexcept {
    return a.m_pos >= b.m_pos;
  }
};

} // namespace detail
} // namespace vapor
