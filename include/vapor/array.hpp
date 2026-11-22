#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/span.hpp>

namespace vapor {

// Fixed-size array, API-compatible with std::array. It is an aggregate, so
// `Array<int, 3> a = {1, 2, 3};` works, and it never allocates. `elems` is
// public only so that brace-initialisation works.
//
// The only deviation from std::array: at() aborts on a bad index (std::array
// throws). checked_at() is the non-trapping alternative.
template <typename T, size_t N> struct Array {
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T &reference;
  typedef const T &const_reference;
  typedef T *pointer;
  typedef const T *const_pointer;
  typedef T *iterator;
  typedef const T *const_iterator;

  T elems[N];

  VAPOR_NODISCARD constexpr size_type size() const noexcept { return N; }
  VAPOR_NODISCARD constexpr size_type max_size() const noexcept { return N; }
  VAPOR_NODISCARD constexpr bool empty() const noexcept { return false; }

  VAPOR_NODISCARD reference operator[](size_type pos) noexcept {
    assert(pos < N);
    return elems[pos];
  }
  VAPOR_NODISCARD const_reference operator[](size_type pos) const noexcept {
    assert(pos < N);
    return elems[pos];
  }
  VAPOR_NODISCARD reference at(size_type pos) noexcept {
    if (pos >= N)
      abort();
    return elems[pos];
  }
  VAPOR_NODISCARD const_reference at(size_type pos) const noexcept {
    if (pos >= N)
      abort();
    return elems[pos];
  }
  VAPOR_NODISCARD reference front() noexcept { return elems[0]; }
  VAPOR_NODISCARD const_reference front() const noexcept { return elems[0]; }
  VAPOR_NODISCARD reference back() noexcept { return elems[N - 1]; }
  VAPOR_NODISCARD const_reference back() const noexcept {
    return elems[N - 1];
  }
  VAPOR_NODISCARD pointer data() noexcept { return elems; }
  VAPOR_NODISCARD const_pointer data() const noexcept { return elems; }

  VAPOR_NODISCARD iterator begin() noexcept { return elems; }
  VAPOR_NODISCARD const_iterator begin() const noexcept { return elems; }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return elems; }
  VAPOR_NODISCARD iterator end() noexcept { return elems + N; }
  VAPOR_NODISCARD const_iterator end() const noexcept { return elems + N; }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return elems + N; }

  // Non-trapping access (vapor extension)
  VAPOR_NODISCARD Expected<ReferenceWrapper<T>, CommonError>
  checked_at(size_type pos) noexcept {
    if (pos >= N)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<T>(elems[pos]);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const T>, CommonError>
  checked_at(size_type pos) const noexcept {
    if (pos >= N)
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const T>(elems[pos]);
  }

  void fill(const T &value) {
    for (size_type i = 0; i < N; ++i)
      elems[i] = value;
  }

  void swap(Array &other) {
    for (size_type i = 0; i < N; ++i) {
      T tmp(Move(elems[i]));
      elems[i] = Move(other.elems[i]);
      other.elems[i] = Move(tmp);
    }
  }

  operator Span<T>() noexcept { return Span<T>(elems, N); }
  operator Span<const T>() const noexcept { return Span<const T>(elems, N); }
};

// Zero-length specialisation: no storage at all.
template <typename T> struct Array<T, 0> {
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T &reference;
  typedef const T &const_reference;
  typedef T *pointer;
  typedef const T *const_pointer;
  typedef T *iterator;
  typedef const T *const_iterator;

  VAPOR_NODISCARD constexpr size_type size() const noexcept { return 0; }
  VAPOR_NODISCARD constexpr size_type max_size() const noexcept { return 0; }
  VAPOR_NODISCARD constexpr bool empty() const noexcept { return true; }
  VAPOR_NODISCARD pointer data() noexcept { return nullptr; }
  VAPOR_NODISCARD const_pointer data() const noexcept { return nullptr; }
  VAPOR_NODISCARD iterator begin() noexcept { return nullptr; }
  VAPOR_NODISCARD const_iterator begin() const noexcept { return nullptr; }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return nullptr; }
  VAPOR_NODISCARD iterator end() noexcept { return nullptr; }
  VAPOR_NODISCARD const_iterator end() const noexcept { return nullptr; }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return nullptr; }

  VAPOR_NODISCARD Expected<ReferenceWrapper<T>, CommonError>
  checked_at(size_type) noexcept {
    return MakeUnexpected(CommonError::OutOfBounds);
  }
  void fill(const T &) {}
  void swap(Array &) {}
};

template <typename T, size_t N>
VAPOR_NODISCARD bool operator==(const Array<T, N> &a, const Array<T, N> &b) {
  for (size_t i = 0; i < N; ++i)
    if (!(a.elems[i] == b.elems[i]))
      return false;
  return true;
}
template <typename T, size_t N>
VAPOR_NODISCARD bool operator!=(const Array<T, N> &a, const Array<T, N> &b) {
  return !(a == b);
}

#if __cplusplus >= 201703L
template <typename T, typename... U>
Array(T, U...) -> Array<T, 1 + sizeof...(U)>;
#endif

} // namespace vapor
