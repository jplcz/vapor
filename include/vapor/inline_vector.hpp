#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/span.hpp>

namespace vapor {

// A vector whose storage lives inside the object (capacity N, fixed at
// compile time), so it never allocates: handy for kernels, interrupt
// handlers and stack buffers. API follows std::vector; deviations are the
// same as Vector: operations that can fail (here: the capacity is exhausted)
// return Expected<..., CommonError> with CapacityExceeded, and at() aborts.
//
// Because copying never allocates, InlineVector is copyable (Vector is not).
template <typename T, size_t N> class InlineVector {
  static_assert(N > 0, "InlineVector capacity must be greater than zero");

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
  alignas(T) unsigned char m_storage[N * sizeof(T)];
  size_type m_size;

  T *Ptr() noexcept { return reinterpret_cast<T *>(m_storage); }
  const T *Ptr() const noexcept {
    return reinterpret_cast<const T *>(m_storage);
  }

  Expected<iterator, CommonError> InsertAt(size_type idx, T &&value) {
    if (m_size == N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    T *d = Ptr();
    if (idx == m_size) {
      detail::ConstructAt<T>(d + m_size, Move(value));
    } else {
      detail::ConstructAt<T>(d + m_size, Move(d[m_size - 1]));
      detail::MoveAssignBackward(d + idx, d + m_size - 1, d + m_size);
      d[idx] = Move(value);
    }
    ++m_size;
    return d + idx;
  }

public:
  InlineVector() noexcept : m_size(0) {}

  InlineVector(const InlineVector &other) : m_size(other.m_size) {
    detail::CopyConstructRange(Ptr(), other.Ptr(), m_size);
  }

  InlineVector(InlineVector &&other) noexcept : m_size(other.m_size) {
    detail::MoveConstructRange(Ptr(), other.Ptr(), m_size);
    other.clear();
  }

  InlineVector &operator=(const InlineVector &other) {
    if (this != &other) {
      clear();
      detail::CopyConstructRange(Ptr(), other.Ptr(), other.m_size);
      m_size = other.m_size;
    }
    return *this;
  }

  InlineVector &operator=(InlineVector &&other) noexcept {
    if (this != &other) {
      clear();
      detail::MoveConstructRange(Ptr(), other.Ptr(), other.m_size);
      m_size = other.m_size;
      other.clear();
    }
    return *this;
  }

  ~InlineVector() { clear(); }

  // --- Capacity ---
  VAPOR_NODISCARD size_type size() const noexcept { return m_size; }
  VAPOR_NODISCARD static constexpr size_type capacity() noexcept { return N; }
  VAPOR_NODISCARD static constexpr size_type max_size() noexcept { return N; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }
  VAPOR_NODISCARD bool full() const noexcept { return m_size == N; }

  // --- Element access ---
  VAPOR_NODISCARD reference operator[](size_type pos) noexcept {
    assert(pos < m_size);
    return Ptr()[pos];
  }
  VAPOR_NODISCARD const_reference operator[](size_type pos) const noexcept {
    assert(pos < m_size);
    return Ptr()[pos];
  }
  VAPOR_NODISCARD reference at(size_type pos) noexcept {
    if (pos >= m_size)
      abort();
    return Ptr()[pos];
  }
  VAPOR_NODISCARD const_reference at(size_type pos) const noexcept {
    if (pos >= m_size)
      abort();
    return Ptr()[pos];
  }
  VAPOR_NODISCARD reference front() noexcept {
    assert(m_size);
    return Ptr()[0];
  }
  VAPOR_NODISCARD const_reference front() const noexcept {
    assert(m_size);
    return Ptr()[0];
  }
  VAPOR_NODISCARD reference back() noexcept {
    assert(m_size);
    return Ptr()[m_size - 1];
  }
  VAPOR_NODISCARD const_reference back() const noexcept {
    assert(m_size);
    return Ptr()[m_size - 1];
  }
  VAPOR_NODISCARD pointer data() noexcept { return Ptr(); }
  VAPOR_NODISCARD const_pointer data() const noexcept { return Ptr(); }

  // Non-trapping access (vapor extension)
  VAPOR_NODISCARD Expected<ReferenceWrapper<T>, CommonError>
  checked_at(size_type pos) noexcept {
    if (pos >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<T>(Ptr()[pos]);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const T>, CommonError>
  checked_at(size_type pos) const noexcept {
    if (pos >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const T>(Ptr()[pos]);
  }

  // --- Iterators ---
  VAPOR_NODISCARD iterator begin() noexcept { return Ptr(); }
  VAPOR_NODISCARD const_iterator begin() const noexcept { return Ptr(); }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return Ptr(); }
  VAPOR_NODISCARD iterator end() noexcept { return Ptr() + m_size; }
  VAPOR_NODISCARD const_iterator end() const noexcept {
    return Ptr() + m_size;
  }
  VAPOR_NODISCARD const_iterator cend() const noexcept {
    return Ptr() + m_size;
  }

  // --- Modifiers ---
  void clear() noexcept {
    detail::DestroyRange(Ptr(), m_size);
    m_size = 0;
  }

  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_back(Args &&...args) {
    if (m_size == N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    detail::ConstructAt<T>(Ptr() + m_size, Forward<Args>(args)...);
    ++m_size;
    return ReferenceWrapper<T>(Ptr()[m_size - 1]);
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
    detail::DestroyAt(Ptr() + m_size - 1);
    --m_size;
  }

  Expected<iterator, CommonError> insert(const_iterator pos, const T &value) {
    if (pos < Ptr() || pos > Ptr() + m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    size_type idx = static_cast<size_type>(pos - Ptr());
    T tmp(value); // value may live inside this vector
    return InsertAt(idx, Move(tmp));
  }

  Expected<iterator, CommonError> insert(const_iterator pos, T &&value) {
    if (pos < Ptr() || pos > Ptr() + m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return InsertAt(static_cast<size_type>(pos - Ptr()), Move(value));
  }

  iterator erase(const_iterator pos) noexcept {
    assert(pos >= Ptr() && pos < Ptr() + m_size);
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) noexcept {
    T *d = Ptr();
    assert(first >= d && first <= last && last <= d + m_size);
    size_type a = static_cast<size_type>(first - d);
    size_type b = static_cast<size_type>(last - d);
    if (a == b)
      return d + a;
    detail::MoveAssignForward(d + b, d + m_size, d + a);
    size_type removed = b - a;
    detail::DestroyRange(d + m_size - removed, removed);
    m_size -= removed;
    return d + a;
  }

  Expected<void, CommonError> resize(size_type n) {
    if (n <= m_size) {
      detail::DestroyRange(Ptr() + n, m_size - n);
      m_size = n;
      return {};
    }
    if (n > N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    for (; m_size < n; ++m_size)
      detail::ConstructAt<T>(Ptr() + m_size);
    return {};
  }

  Expected<void, CommonError> resize(size_type n, const T &value) {
    if (n <= m_size)
      return resize(n);
    if (n > N)
      return MakeUnexpected(CommonError::CapacityExceeded);
    T tmp(value); // value may live inside this vector
    for (; m_size < n; ++m_size)
      detail::ConstructAt<T>(Ptr() + m_size, tmp);
    return {};
  }

  // Appends a range (vapor extension). All-or-nothing: nothing is added if it
  // would not fit.
  Expected<void, CommonError> append(Span<const T> range) {
    if (range.size() > N - m_size)
      return MakeUnexpected(CommonError::CapacityExceeded);
    detail::CopyConstructRange(Ptr() + m_size, range.data(), range.size());
    m_size += range.size();
    return {};
  }

  void swap(InlineVector &other) {
    InlineVector tmp(Move(other));
    other = Move(*this);
    *this = Move(tmp);
  }

  // --- Span views ---
  operator Span<T>() noexcept { return Span<T>(Ptr(), m_size); }
  operator Span<const T>() const noexcept {
    return Span<const T>(Ptr(), m_size);
  }
};

template <typename T, size_t N>
VAPOR_NODISCARD bool operator==(const InlineVector<T, N> &a,
                                const InlineVector<T, N> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!(a[i] == b[i]))
      return false;
  return true;
}
template <typename T, size_t N>
VAPOR_NODISCARD bool operator!=(const InlineVector<T, N> &a,
                                const InlineVector<T, N> &b) {
  return !(a == b);
}

} // namespace vapor
