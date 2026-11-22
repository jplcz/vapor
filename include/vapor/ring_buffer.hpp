#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/common.hpp>
#include <vapor/detail/index_iterator.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>

// RingBuffer<T, N>: a first-in-first-out queue with fixed capacity N, stored
// inside the object (no allocation), the classic "circular buffer".
//
// Why: producers and consumers (log lines, audio samples, serial bytes,
// events between interrupt handlers and the main loop) need a queue that
// never allocates and has a hard memory bound. When it is full you decide
// what happens: refuse the new item (push_back, returns CapacityExceeded) or
// forget the oldest one (PushOverwrite, for "keep the latest N" histories).
//
// How:
//   RingBuffer<int, 4> rb;
//   (void)rb.push_back(1);               // Expected<void, CommonError>
//   rb.PushOverwrite(5);                 // never fails; may drop the oldest
//   rb.front(); rb.back(); rb[i];        // oldest ... newest
//   rb.pop_front();                      // remove the oldest
//   for (int x : rb) ...                 // oldest to newest
//
// Single-threaded: it is NOT safe to push from one thread while popping from
// another. Copying copies all elements (no allocation, so it cannot fail).

namespace vapor {

template <typename T, size_t N> class RingBuffer {
  static_assert(N > 0, "RingBuffer capacity must be greater than zero");

public:
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T &reference;
  typedef const T &const_reference;
  typedef detail::IndexIterator<RingBuffer, T> iterator;
  typedef detail::IndexIterator<const RingBuffer, const T> const_iterator;

private:
  alignas(T) unsigned char m_storage[N * sizeof(T)];
  size_type m_head;
  size_type m_size;

  T *Ptr() noexcept { return reinterpret_cast<T *>(m_storage); }
  const T *Ptr() const noexcept {
    return reinterpret_cast<const T *>(m_storage);
  }
  size_type Slot(size_type i) const noexcept { return (m_head + i) % N; }

public:
  RingBuffer() noexcept : m_head(0), m_size(0) {}
  RingBuffer(const RingBuffer &o) : m_head(0), m_size(0) {
    for (size_type i = 0; i < o.m_size; ++i)
      detail::ConstructAt<T>(Ptr() + i, o[i]), ++m_size;
  }
  RingBuffer(RingBuffer &&o) noexcept : m_head(0), m_size(0) {
    for (size_type i = 0; i < o.m_size; ++i)
      detail::ConstructAt<T>(Ptr() + i, Move(o[i])), ++m_size;
    o.clear();
  }
  RingBuffer &operator=(const RingBuffer &o) {
    if (this != &o) {
      clear();
      for (size_type i = 0; i < o.m_size; ++i)
        detail::ConstructAt<T>(Ptr() + i, o[i]), ++m_size;
    }
    return *this;
  }
  RingBuffer &operator=(RingBuffer &&o) noexcept {
    if (this != &o) {
      clear();
      for (size_type i = 0; i < o.m_size; ++i)
        detail::ConstructAt<T>(Ptr() + i, Move(o[i])), ++m_size;
      o.clear();
    }
    return *this;
  }
  ~RingBuffer() { clear(); }

  VAPOR_NODISCARD size_type size() const noexcept { return m_size; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }
  VAPOR_NODISCARD bool full() const noexcept { return m_size == N; }
  VAPOR_NODISCARD size_type capacity() const noexcept { return N; }
  VAPOR_NODISCARD size_type max_size() const noexcept { return N; }

  VAPOR_NODISCARD reference operator[](size_type i) noexcept {
    assert(i < m_size);
    return Ptr()[Slot(i)];
  }
  VAPOR_NODISCARD const_reference operator[](size_type i) const noexcept {
    assert(i < m_size);
    return Ptr()[Slot(i)];
  }
  VAPOR_NODISCARD reference at(size_type i) noexcept {
    if (i >= m_size)
      abort();
    return Ptr()[Slot(i)];
  }
  VAPOR_NODISCARD const_reference at(size_type i) const noexcept {
    if (i >= m_size)
      abort();
    return Ptr()[Slot(i)];
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<T>, CommonError>
  checked_at(size_type i) noexcept {
    if (i >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<T>(Ptr()[Slot(i)]);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const T>, CommonError>
  checked_at(size_type i) const noexcept {
    if (i >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const T>(Ptr()[Slot(i)]);
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

  // Adds at the back; CapacityExceeded (and no change) when full.
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_back(Args &&...args) {
    if (m_size == N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    T *p = detail::ConstructAt<T>(Ptr() + Slot(m_size), Forward<Args>(args)...);
    ++m_size;
    return ReferenceWrapper<T>(*p);
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

  // Adds at the back; when full the oldest element is dropped first.
  // Returns true if an element was dropped.
  bool PushOverwrite(T v) {
    bool dropped = false;
    if (m_size == N) {
      pop_front();
      dropped = true;
    }
    detail::ConstructAt<T>(Ptr() + Slot(m_size), Move(v));
    ++m_size;
    return dropped;
  }

  void pop_front() noexcept {
    assert(m_size);
    detail::DestroyAt(Ptr() + m_head);
    m_head = (m_head + 1) % N;
    --m_size;
  }
  void pop_back() noexcept {
    assert(m_size);
    detail::DestroyAt(Ptr() + Slot(m_size - 1));
    --m_size;
  }

  void clear() noexcept {
    while (m_size != 0)
      pop_front();
    m_head = 0;
  }
};

} // namespace vapor
