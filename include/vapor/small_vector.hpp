#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/span.hpp>

// SmallVector<T, N>: a Vector that keeps its first N elements inside the
// object and only allocates when it grows beyond that.
//
// Why: most lists are short. Allocating for a list of 3 items is slow and
// fragments memory; InlineVector avoids that but can never hold more than N.
// SmallVector gives both: no allocation in the common case, graceful growth
// in the rare one (a good fit for "usually a few children, sometimes
// hundreds").
//
// How: same API as Vector (see vector.hpp): push_back/emplace_back/insert/
// resize/reserve return Expected<..., CommonError> (OutOfMemory when the
// allocator fails, container unchanged), at() aborts, checked_at() reports,
// copying is the explicit Clone(). `is_inline()` tells whether the elements
// still live inside the object. Pointers and iterators are invalidated by
// growth and by moving the vector (when inline the elements are moved
// element by element).

namespace vapor {

template <typename T, size_t N> class SmallVector {
  static_assert(N > 0, "SmallVector inline capacity must be greater than 0");

public:
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T &reference;
  typedef const T &const_reference;
  typedef T *pointer;
  typedef const T *const_pointer;
  typedef T *iterator;
  typedef const T *const_iterator;

private:
  Allocator *m_alloc;
  T *m_data;
  size_type m_size;
  size_type m_capacity;
  alignas(T) unsigned char m_inline[N * sizeof(T)];

  T *Inline() noexcept { return reinterpret_cast<T *>(m_inline); }
  bool OnHeap() const noexcept {
    return m_data != reinterpret_cast<const T *>(m_inline);
  }

  Expected<void, CommonError> SetCapacity(size_type n) noexcept {
    assert(n >= m_size);
    if (n == m_capacity)
      return {};
    if (n <= N) { // shrink back into the inline buffer
      if (!OnHeap())
        return {};
      T *heap = m_data;
      size_type cap = m_capacity;
      detail::MoveConstructRange(Inline(), heap, m_size);
      detail::DestroyRange(heap, m_size);
      m_alloc->Deallocate(heap, cap * sizeof(T));
      m_data = Inline();
      m_capacity = N;
      return {};
    }
    if (n > VAPOR_SIZE_MAX / sizeof(T))
      return MakeUnexpected(CommonError::OutOfMemory);
    void *p = m_alloc->Allocate(n * sizeof(T), alignof(T));
    if (!p)
      return MakeUnexpected(CommonError::OutOfMemory);
    T *fresh = static_cast<T *>(p);
    detail::MoveConstructRange(fresh, m_data, m_size);
    detail::DestroyRange(m_data, m_size);
    if (OnHeap())
      m_alloc->Deallocate(m_data, m_capacity * sizeof(T));
    m_data = fresh;
    m_capacity = n;
    return {};
  }

  Expected<void, CommonError> Grow(size_type required) noexcept {
    if (required <= m_capacity)
      return {};
    size_type cap = m_capacity;
    if (cap <= VAPOR_SIZE_MAX / 2)
      cap *= 2;
    if (cap < required)
      cap = required;
    return SetCapacity(cap);
  }

  Expected<iterator, CommonError> InsertAt(size_type idx, T &&value) {
    Expected<void, CommonError> res = Grow(m_size + 1);
    if (!res)
      return MakeUnexpected(res.error());
    if (idx == m_size) {
      detail::ConstructAt<T>(m_data + m_size, Move(value));
    } else {
      detail::ConstructAt<T>(m_data + m_size, Move(m_data[m_size - 1]));
      detail::MoveAssignBackward(m_data + idx, m_data + m_size - 1,
                                 m_data + m_size);
      m_data[idx] = Move(value);
    }
    ++m_size;
    return m_data + idx;
  }

  void ResetToInline() noexcept {
    m_data = Inline();
    m_size = 0;
    m_capacity = N;
  }

  void ReleaseAll() noexcept {
    detail::DestroyRange(m_data, m_size);
    if (OnHeap())
      m_alloc->Deallocate(m_data, m_capacity * sizeof(T));
    ResetToInline();
  }

  // Takes over other's elements; this must be empty and inline.
  void TakeFrom(SmallVector &other) noexcept {
    if (other.OnHeap()) {
      m_data = other.m_data;
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      other.ResetToInline();
    } else {
      detail::MoveConstructRange(Inline(), other.m_data, other.m_size);
      m_size = other.m_size;
      other.clear();
    }
  }

public:
  explicit SmallVector(
      Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_alloc(&alloc), m_data(Inline()), m_size(0), m_capacity(N) {}

  SmallVector(SmallVector &&other) noexcept
      : m_alloc(other.m_alloc), m_data(Inline()), m_size(0), m_capacity(N) {
    TakeFrom(other);
  }

  SmallVector &operator=(SmallVector &&other) noexcept {
    if (this != &other) {
      ReleaseAll();
      m_alloc = other.m_alloc;
      TakeFrom(other);
    }
    return *this;
  }

  SmallVector(const SmallVector &) = delete;
  SmallVector &operator=(const SmallVector &) = delete;

  ~SmallVector() { ReleaseAll(); }

  VAPOR_NODISCARD Expected<SmallVector, CommonError> Clone() const {
    SmallVector copy(*m_alloc);
    Expected<void, CommonError> res = copy.reserve(m_size);
    if (!res)
      return MakeUnexpected(res.error());
    detail::CopyConstructRange(copy.m_data, m_data, m_size);
    copy.m_size = m_size;
    return copy;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept { return *m_alloc; }

  VAPOR_NODISCARD size_type size() const noexcept { return m_size; }
  VAPOR_NODISCARD size_type capacity() const noexcept { return m_capacity; }
  VAPOR_NODISCARD size_type max_size() const noexcept {
    return VAPOR_SIZE_MAX / sizeof(T);
  }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }
  // True while the elements live inside the object (no heap allocation).
  VAPOR_NODISCARD bool is_inline() const noexcept { return !OnHeap(); }

  Expected<void, CommonError> reserve(size_type n) noexcept {
    if (n <= m_capacity)
      return {};
    return SetCapacity(n);
  }
  // Moves back into the inline buffer when the elements fit again.
  Expected<void, CommonError> shrink_to_fit() noexcept {
    return SetCapacity(m_size < N ? N : m_size);
  }

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
    assert(m_size);
    return m_data[0];
  }
  VAPOR_NODISCARD const_reference front() const noexcept {
    assert(m_size);
    return m_data[0];
  }
  VAPOR_NODISCARD reference back() noexcept {
    assert(m_size);
    return m_data[m_size - 1];
  }
  VAPOR_NODISCARD const_reference back() const noexcept {
    assert(m_size);
    return m_data[m_size - 1];
  }
  VAPOR_NODISCARD pointer data() noexcept { return m_data; }
  VAPOR_NODISCARD const_pointer data() const noexcept { return m_data; }

  VAPOR_NODISCARD Expected<ReferenceWrapper<T>, CommonError>
  checked_at(size_type pos) noexcept {
    if (pos >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<T>(m_data[pos]);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const T>, CommonError>
  checked_at(size_type pos) const noexcept {
    if (pos >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const T>(m_data[pos]);
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

  void clear() noexcept {
    detail::DestroyRange(m_data, m_size);
    m_size = 0;
  }

  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_back(Args &&...args) {
    if (m_size < m_capacity) {
      detail::ConstructAt<T>(m_data + m_size, Forward<Args>(args)...);
    } else {
      T tmp(Forward<Args>(args)...); // args may refer into this vector
      Expected<void, CommonError> res = Grow(m_size + 1);
      if (!res)
        return MakeUnexpected(res.error());
      detail::ConstructAt<T>(m_data + m_size, Move(tmp));
    }
    ++m_size;
    return ReferenceWrapper<T>(m_data[m_size - 1]);
  }
  Expected<void, CommonError> push_back(const T &value) {
    Expected<ReferenceWrapper<T>, CommonError> res = emplace_back(value);
    if (!res)
      return MakeUnexpected(res.error());
    return {};
  }
  Expected<void, CommonError> push_back(T &&value) {
    Expected<ReferenceWrapper<T>, CommonError> res = emplace_back(Move(value));
    if (!res)
      return MakeUnexpected(res.error());
    return {};
  }
  void pop_back() noexcept {
    assert(m_size);
    detail::DestroyAt(m_data + m_size - 1);
    --m_size;
  }

  Expected<iterator, CommonError> insert(const_iterator pos, const T &value) {
    if (pos < m_data || pos > m_data + m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    size_type idx = static_cast<size_type>(pos - m_data);
    T tmp(value);
    return InsertAt(idx, Move(tmp));
  }
  Expected<iterator, CommonError> insert(const_iterator pos, T &&value) {
    if (pos < m_data || pos > m_data + m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return InsertAt(static_cast<size_type>(pos - m_data), Move(value));
  }

  iterator erase(const_iterator pos) noexcept {
    assert(pos >= m_data && pos < m_data + m_size);
    return erase(pos, pos + 1);
  }
  iterator erase(const_iterator first, const_iterator last) noexcept {
    assert(first >= m_data && first <= last && last <= m_data + m_size);
    size_type a = static_cast<size_type>(first - m_data);
    size_type b = static_cast<size_type>(last - m_data);
    if (a == b)
      return m_data + a;
    detail::MoveAssignForward(m_data + b, m_data + m_size, m_data + a);
    size_type removed = b - a;
    detail::DestroyRange(m_data + m_size - removed, removed);
    m_size -= removed;
    return m_data + a;
  }

  Expected<void, CommonError> resize(size_type n) {
    if (n <= m_size) {
      detail::DestroyRange(m_data + n, m_size - n);
      m_size = n;
      return {};
    }
    Expected<void, CommonError> res = Grow(n);
    if (!res)
      return res;
    for (; m_size < n; ++m_size)
      detail::ConstructAt<T>(m_data + m_size);
    return {};
  }

  // Appends a range (the range may alias this vector).
  Expected<void, CommonError> append(Span<const T> range) {
    size_type n = range.size();
    if (n == 0)
      return {};
    if (n > max_size() - m_size)
      return MakeUnexpected(CommonError::OutOfMemory);
    bool inside = detail::PointsInto(range.data(), m_data, m_size);
    size_type offset =
        inside ? static_cast<size_type>(range.data() - m_data) : 0;
    Expected<void, CommonError> res = Grow(m_size + n);
    if (!res)
      return res;
    const T *src = inside ? m_data + offset : range.data();
    detail::CopyConstructRange(m_data + m_size, src, n);
    m_size += n;
    return {};
  }

  void swap(SmallVector &other) noexcept {
    SmallVector tmp(Move(other));
    other = Move(*this);
    *this = Move(tmp);
  }

  operator Span<T>() noexcept { return Span<T>(m_data, m_size); }
  operator Span<const T>() const noexcept {
    return Span<const T>(m_data, m_size);
  }
};

} // namespace vapor
