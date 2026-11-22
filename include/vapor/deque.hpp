#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/index_iterator.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>

// Deque<T>: a double-ended queue with O(1) push and pop at BOTH ends and O(1)
// random access, modelled on std::deque.
//
// Why: Vector is fast at the back but erasing from the front shifts every
// element. Queues, sliding windows, undo histories and breadth-first search
// all add at one end and remove at the other; a Deque does that without
// shifting anything.
//
// How:
//   Deque<int> q;
//   (void)q.push_back(1);              // Expected<void, CommonError>
//   (void)q.push_front(0);
//   q.front(); q.back(); q[1];         // access (assert-checked)
//   q.pop_front(); q.pop_back();
//   for (int x : q) ...                // front to back
//
// Unlike std::deque the elements live in ONE circular buffer (not many
// blocks), so growing moves all elements and invalidates every iterator and
// reference; push/pop without growth only invalidate iterators (references to
// other elements stay valid). In exchange there is a single allocation and
// good cache behaviour. Differences forced by the lack of exceptions:
// operations that may allocate return Expected and leave the Deque unchanged
// on failure; copying is the explicit, fallible Clone(); at() aborts and
// checked_at() reports OutOfBounds. insert()/erase() in the middle are not
// provided (use push/pop at the ends, or Vector).

namespace vapor {

template <typename T> class Deque {
public:
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T &reference;
  typedef const T &const_reference;
  typedef detail::IndexIterator<Deque, T> iterator;
  typedef detail::IndexIterator<const Deque, const T> const_iterator;

private:
  Allocator *m_alloc;
  T *m_buf;
  size_type m_cap; // zero or a power of two
  size_type m_head;
  size_type m_size;

  size_type Slot(size_type i) const noexcept {
    return (m_head + i) & (m_cap - 1);
  }

  // Reallocates to `new_cap` (a power of two >= size), keeping the order.
  Expected<void, CommonError> SetCapacity(size_type new_cap) noexcept {
    assert(new_cap >= m_size);
    if (new_cap > VAPOR_SIZE_MAX / sizeof(T))
      return MakeUnexpected(CommonError::OutOfMemory);
    void *p = m_alloc->Allocate(new_cap * sizeof(T), alignof(T));
    if (!p)
      return MakeUnexpected(CommonError::OutOfMemory);
    T *fresh = static_cast<T *>(p);
    for (size_type i = 0; i < m_size; ++i) {
      detail::ConstructAt<T>(fresh + i, Move(m_buf[Slot(i)]));
      detail::DestroyAt(m_buf + Slot(i));
    }
    if (m_buf)
      m_alloc->Deallocate(m_buf, m_cap * sizeof(T));
    m_buf = fresh;
    m_cap = new_cap;
    m_head = 0;
    return {};
  }

  Expected<void, CommonError> GrowForOne() noexcept {
    if (m_size < m_cap)
      return {};
    size_type cap = m_cap ? m_cap : 8;
    if (m_cap) {
      if (m_cap > VAPOR_SIZE_MAX / 2)
        return MakeUnexpected(CommonError::OutOfMemory);
      cap = m_cap * 2;
    }
    return SetCapacity(cap);
  }

  void ReleaseAll() noexcept {
    for (size_type i = 0; i < m_size; ++i)
      detail::DestroyAt(m_buf + Slot(i));
    if (m_buf)
      m_alloc->Deallocate(m_buf, m_cap * sizeof(T));
    m_buf = nullptr;
    m_cap = m_head = m_size = 0;
  }

public:
  explicit Deque(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_alloc(&alloc), m_buf(nullptr), m_cap(0), m_head(0), m_size(0) {}

  Deque(Deque &&o) noexcept
      : m_alloc(o.m_alloc), m_buf(o.m_buf), m_cap(o.m_cap), m_head(o.m_head),
        m_size(o.m_size) {
    o.m_buf = nullptr;
    o.m_cap = o.m_head = o.m_size = 0;
  }
  Deque &operator=(Deque &&o) noexcept {
    if (this != &o) {
      ReleaseAll();
      m_alloc = o.m_alloc;
      m_buf = o.m_buf;
      m_cap = o.m_cap;
      m_head = o.m_head;
      m_size = o.m_size;
      o.m_buf = nullptr;
      o.m_cap = o.m_head = o.m_size = 0;
    }
    return *this;
  }
  Deque(const Deque &) = delete;
  Deque &operator=(const Deque &) = delete;
  ~Deque() { ReleaseAll(); }

  VAPOR_NODISCARD Expected<Deque, CommonError> Clone() const {
    Deque copy(*m_alloc);
    if (m_size != 0) {
      size_type cap = 8;
      while (cap < m_size)
        cap *= 2;
      Expected<void, CommonError> r = copy.SetCapacity(cap);
      if (!r)
        return MakeUnexpected(r.error());
      for (size_type i = 0; i < m_size; ++i)
        detail::ConstructAt<T>(copy.m_buf + i, m_buf[Slot(i)]);
      copy.m_size = m_size;
    }
    return copy;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept { return *m_alloc; }
  VAPOR_NODISCARD size_type size() const noexcept { return m_size; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }
  VAPOR_NODISCARD size_type capacity() const noexcept { return m_cap; }
  VAPOR_NODISCARD size_type max_size() const noexcept {
    return VAPOR_SIZE_MAX / sizeof(T) / 2;
  }

  // Makes room for at least `n` elements so that pushes up to that size
  // cannot fail.
  Expected<void, CommonError> reserve(size_type n) noexcept {
    if (n <= m_cap)
      return {};
    size_type cap = m_cap ? m_cap : 8;
    while (cap < n) {
      if (cap > VAPOR_SIZE_MAX / 2)
        return MakeUnexpected(CommonError::OutOfMemory);
      cap *= 2;
    }
    return SetCapacity(cap);
  }

  VAPOR_NODISCARD reference operator[](size_type i) noexcept {
    assert(i < m_size);
    return m_buf[Slot(i)];
  }
  VAPOR_NODISCARD const_reference operator[](size_type i) const noexcept {
    assert(i < m_size);
    return m_buf[Slot(i)];
  }
  VAPOR_NODISCARD reference at(size_type i) noexcept {
    if (i >= m_size)
      abort();
    return m_buf[Slot(i)];
  }
  VAPOR_NODISCARD const_reference at(size_type i) const noexcept {
    if (i >= m_size)
      abort();
    return m_buf[Slot(i)];
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<T>, CommonError>
  checked_at(size_type i) noexcept {
    if (i >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<T>(m_buf[Slot(i)]);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const T>, CommonError>
  checked_at(size_type i) const noexcept {
    if (i >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const T>(m_buf[Slot(i)]);
  }
  VAPOR_NODISCARD reference front() noexcept { return (*this)[0]; }
  VAPOR_NODISCARD const_reference front() const noexcept { return (*this)[0]; }
  VAPOR_NODISCARD reference back() noexcept {
    assert(m_size);
    return (*this)[m_size - 1];
  }
  VAPOR_NODISCARD const_reference back() const noexcept {
    assert(m_size);
    return (*this)[m_size - 1];
  }

  VAPOR_NODISCARD iterator begin() noexcept { return iterator(this, 0); }
  VAPOR_NODISCARD iterator end() noexcept { return iterator(this, m_size); }
  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return const_iterator(this, 0);
  }
  VAPOR_NODISCARD const_iterator end() const noexcept {
    return const_iterator(this, m_size);
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return begin(); }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return end(); }

  // The element is built before any reallocation, so arguments that refer to
  // an element of this deque stay valid.
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_back(Args &&...args) {
    if (m_size < m_cap) {
      detail::ConstructAt<T>(m_buf + Slot(m_size), Forward<Args>(args)...);
    } else {
      T tmp(Forward<Args>(args)...);
      Expected<void, CommonError> r = GrowForOne();
      if (!r)
        return MakeUnexpected(r.error());
      detail::ConstructAt<T>(m_buf + Slot(m_size), Move(tmp));
    }
    ++m_size;
    return ReferenceWrapper<T>(m_buf[Slot(m_size - 1)]);
  }
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_front(Args &&...args) {
    T tmp(Forward<Args>(args)...);
    Expected<void, CommonError> r = GrowForOne();
    if (!r)
      return MakeUnexpected(r.error());
    m_head = (m_head + m_cap - 1) & (m_cap - 1);
    detail::ConstructAt<T>(m_buf + m_head, Move(tmp));
    ++m_size;
    return ReferenceWrapper<T>(m_buf[m_head]);
  }
  Expected<void, CommonError> push_back(const T &v) {
    Expected<ReferenceWrapper<T>, CommonError> r = emplace_back(v);
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }
  Expected<void, CommonError> push_back(T &&v) {
    Expected<ReferenceWrapper<T>, CommonError> r = emplace_back(Move(v));
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }
  Expected<void, CommonError> push_front(const T &v) {
    Expected<ReferenceWrapper<T>, CommonError> r = emplace_front(v);
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }
  Expected<void, CommonError> push_front(T &&v) {
    Expected<ReferenceWrapper<T>, CommonError> r = emplace_front(Move(v));
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }

  void pop_back() noexcept {
    assert(m_size);
    detail::DestroyAt(m_buf + Slot(m_size - 1));
    --m_size;
  }
  void pop_front() noexcept {
    assert(m_size);
    detail::DestroyAt(m_buf + m_head);
    m_head = (m_head + 1) & (m_cap - 1);
    --m_size;
  }

  void clear() noexcept {
    for (size_type i = 0; i < m_size; ++i)
      detail::DestroyAt(m_buf + Slot(i));
    m_size = 0;
    m_head = 0;
  }

  void swap(Deque &o) noexcept {
    Deque tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }
};

} // namespace vapor
