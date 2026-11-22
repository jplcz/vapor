#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/span.hpp>

namespace vapor {

// Growable, allocator-backed array modelled on std::vector.
//
// Same names and semantics as std::vector (size, capacity, operator[],
// begin/end, pop_back, erase, ...). It deviates only where std::vector would
// throw:
//  * operations that may allocate return Expected<..., CommonError>
//    (OutOfMemory on failure, container left unchanged);
//  * copying is explicit and fallible: use Clone() instead of a copy ctor;
//  * at() aborts on a bad index; checked_at() is the non-trapping variant.
template <typename T> class Vector {
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

  // Change capacity to exactly n (n >= m_size). Leaves the vector untouched on
  // failure.
  Expected<void, CommonError> SetCapacity(size_type n) noexcept {
    assert(n >= m_size);
    if (n == m_capacity)
      return {};
    if (n == 0) {
      m_alloc->Deallocate(m_data, m_capacity * sizeof(T));
      m_data = nullptr;
      m_capacity = 0;
      return {};
    }
    if (n > VAPOR_SIZE_MAX / sizeof(T))
      return MakeUnexpected(CommonError::OutOfMemory);

    // realloc() may move bytes, which is only valid for trivially copyable T.
    if (IsTriviallyCopyable<T>::value && m_data &&
        alignof(T) <= alignof(max_align_t)) {
      void *p = m_alloc->Reallocate(m_data, n * sizeof(T), alignof(T));
      if (!p)
        return MakeUnexpected(CommonError::OutOfMemory);
      m_data = static_cast<T *>(p);
    } else {
      void *p = m_alloc->Allocate(n * sizeof(T), alignof(T));
      if (!p)
        return MakeUnexpected(CommonError::OutOfMemory);
      T *fresh = static_cast<T *>(p);
      detail::MoveConstructRange(fresh, m_data, m_size);
      detail::DestroyRange(m_data, m_size);
      if (m_data)
        m_alloc->Deallocate(m_data, m_capacity * sizeof(T));
      m_data = fresh;
    }
    m_capacity = n;
    return {};
  }

  Expected<void, CommonError> Grow(size_type required) noexcept {
    if (required <= m_capacity)
      return {};
    size_type cap = m_capacity ? m_capacity : 4;
    if (cap <= VAPOR_SIZE_MAX / 2)
      cap *= 2;
    if (cap < required)
      cap = required;
    return SetCapacity(cap);
  }

  Expected<iterator, CommonError> InsertAt(size_type idx, T &&value) {
    auto res = Grow(m_size + 1);
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

public:
  explicit Vector(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_alloc(&alloc), m_data(nullptr), m_size(0), m_capacity(0) {}

  Vector(Vector &&other) noexcept
      : m_alloc(other.m_alloc), m_data(other.m_data), m_size(other.m_size),
        m_capacity(other.m_capacity) {
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
  }

  Vector &operator=(Vector &&other) noexcept {
    if (this != &other) {
      ReleaseAll();
      m_alloc = other.m_alloc;
      m_data = other.m_data;
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      other.m_data = nullptr;
      other.m_size = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  // Copies can fail (allocation), and there are no exceptions to report it,
  // so they are explicit: see Clone().
  Vector(const Vector &) = delete;
  Vector &operator=(const Vector &) = delete;

  ~Vector() { ReleaseAll(); }

  VAPOR_NODISCARD Expected<Vector, CommonError> Clone() const & noexcept {
    Vector copy(*m_alloc);
    auto res = copy.SetCapacity(m_size);
    if (!res)
      return MakeUnexpected(res.error());
    detail::CopyConstructRange(copy.m_data, m_data, m_size);
    copy.m_size = m_size;
    return copy;
  }

  VAPOR_NODISCARD Expected<Vector, CommonError> Clone() && noexcept {
    return Vector(Move(*this));
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept {
    return *m_alloc;
  }

  // --- Capacity ---
  VAPOR_NODISCARD size_type size() const noexcept { return m_size; }
  VAPOR_NODISCARD size_type capacity() const noexcept { return m_capacity; }
  VAPOR_NODISCARD size_type max_size() const noexcept {
    return VAPOR_SIZE_MAX / sizeof(T);
  }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }

  Expected<void, CommonError> reserve(size_type n) noexcept {
    if (n <= m_capacity)
      return {};
    return SetCapacity(n);
  }

  // Failure to shrink is harmless (the vector stays valid), but is reported.
  Expected<void, CommonError> shrink_to_fit() noexcept {
    return SetCapacity(m_size);
  }

  // --- Element access ---
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

  // Non-trapping access (vapor extension)
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

  // --- Iterators ---
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

  // --- Modifiers ---
  void clear() noexcept {
    detail::DestroyRange(m_data, m_size);
    m_size = 0;
  }

  // The element is built before any reallocation, so arguments that refer to
  // an element of this vector (v.push_back(v[0])) stay valid.
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_back(Args &&...args) {
    if (m_size < m_capacity) {
      detail::ConstructAt<T>(m_data + m_size, Forward<Args>(args)...);
    } else {
      T tmp(Forward<Args>(args)...);
      auto res = Grow(m_size + 1);
      if (!res)
        return MakeUnexpected(res.error());
      detail::ConstructAt<T>(m_data + m_size, Move(tmp));
    }
    ++m_size;
    return ReferenceWrapper<T>(m_data[m_size - 1]);
  }

  Expected<void, CommonError> push_back(const T &value) {
    auto res = emplace_back(value);
    if (!res)
      return MakeUnexpected(res.error());
    return {};
  }

  Expected<void, CommonError> push_back(T &&value) {
    auto res = emplace_back(Move(value));
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
    T tmp(value); // value may live inside this vector
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
    auto res = Grow(n);
    if (!res)
      return res;
    for (; m_size < n; ++m_size)
      detail::ConstructAt<T>(m_data + m_size);
    return {};
  }

  Expected<void, CommonError> resize(size_type n, const T &value) {
    if (n <= m_size)
      return resize(n);
    T tmp(value); // value may live inside this vector
    auto res = Grow(n);
    if (!res)
      return res;
    for (; m_size < n; ++m_size)
      detail::ConstructAt<T>(m_data + m_size, tmp);
    return {};
  }

  // Appends a range (vapor extension; the STL spelling is
  // insert(end(), first, last)). The range may alias this vector.
  Expected<void, CommonError> append(Span<const T> range) {
    size_type n = range.size();
    if (n == 0)
      return {};
    if (n > max_size() - m_size)
      return MakeUnexpected(CommonError::OutOfMemory);

    bool inside = detail::PointsInto(range.data(), m_data, m_size);
    size_type offset =
        inside ? static_cast<size_type>(range.data() - m_data) : 0;
    auto res = Grow(m_size + n);
    if (!res)
      return res;
    const T *src = inside ? m_data + offset : range.data();
    detail::CopyConstructRange(m_data + m_size, src, n);
    m_size += n;
    return {};
  }

  void swap(Vector &other) noexcept {
    Allocator *a = m_alloc;
    T *d = m_data;
    size_type s = m_size, c = m_capacity;
    m_alloc = other.m_alloc;
    m_data = other.m_data;
    m_size = other.m_size;
    m_capacity = other.m_capacity;
    other.m_alloc = a;
    other.m_data = d;
    other.m_size = s;
    other.m_capacity = c;
  }

  // --- Span views ---
  operator Span<T>() noexcept { return Span<T>(m_data, m_size); }
  operator Span<const T>() const noexcept {
    return Span<const T>(m_data, m_size);
  }

private:
  void ReleaseAll() noexcept {
    detail::DestroyRange(m_data, m_size);
    if (m_data)
      m_alloc->Deallocate(m_data, m_capacity * sizeof(T));
    m_data = nullptr;
    m_size = 0;
    m_capacity = 0;
  }
};

template <typename T>
VAPOR_NODISCARD bool operator==(const Vector<T> &a, const Vector<T> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!(a[i] == b[i]))
      return false;
  return true;
}
template <typename T>
VAPOR_NODISCARD bool operator!=(const Vector<T> &a, const Vector<T> &b) {
  return !(a == b);
}

} // namespace vapor
