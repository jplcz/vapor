#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vapor/allocator.hpp>
#include <vapor/config.hpp>
#include <vapor/detail/placement_new.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// Internal helpers shared by Vector, InlineVector, Optional and UniquePtr.
//
// Why: all of them manage raw, partially-initialised memory. Writing the
// "construct / destroy / relocate a range" logic once keeps lifetime bugs in
// one place, and lets trivially copyable element types take a fast memcpy path.

namespace vapor {
namespace detail {

template <typename T, typename... Args>
inline T *ConstructAt(void *p, Args &&...args) {
  return ::new (p) T(Forward<Args>(args)...);
}

template <typename T> inline void DestroyAt(T *p) noexcept { p->~T(); }

template <typename T> inline void DestroyRange(T *first, size_t n) noexcept {
  for (size_t i = 0; i < n; ++i)
    first[i].~T();
}

// True when [data, data + n) contains p (works for unrelated pointers).
template <typename T>
inline bool PointsInto(const T *p, const T *data, size_t n) noexcept {
  uintptr_t a = reinterpret_cast<uintptr_t>(p);
  uintptr_t b = reinterpret_cast<uintptr_t>(data);
  return n != 0 && a >= b && a < b + n * sizeof(T);
}

// Copy-construct n elements into raw memory (ranges must not overlap).
template <typename T>
inline void CopyConstructRange(T *dst, const T *src, size_t n, TrueType) {
  if (n)
    ::memcpy(static_cast<void *>(dst), static_cast<const void *>(src),
             n * sizeof(T));
}
template <typename T>
inline void CopyConstructRange(T *dst, const T *src, size_t n, FalseType) {
  for (size_t i = 0; i < n; ++i)
    ::new (static_cast<void *>(dst + i)) T(src[i]);
}
template <typename T>
inline void CopyConstructRange(T *dst, const T *src, size_t n) {
  CopyConstructRange(dst, src, n,
                     IntegralConstant<bool, IsTriviallyCopyable<T>::value>());
}

// Move-construct n elements into raw memory; sources stay alive (caller
// destroys them).
template <typename T>
inline void MoveConstructRange(T *dst, T *src, size_t n, TrueType) {
  if (n)
    ::memcpy(static_cast<void *>(dst), static_cast<const void *>(src),
             n * sizeof(T));
}
template <typename T>
inline void MoveConstructRange(T *dst, T *src, size_t n, FalseType) {
  for (size_t i = 0; i < n; ++i)
    ::new (static_cast<void *>(dst + i)) T(Move(src[i]));
}
template <typename T> inline void MoveConstructRange(T *dst, T *src, size_t n) {
  MoveConstructRange(dst, src, n,
                     IntegralConstant<bool, IsTriviallyCopyable<T>::value>());
}

// Assign [first, last) onto the live elements ending at dst_last, back to
// front (used to open a gap for insert).
template <typename T>
inline void MoveAssignBackward(T *first, T *last, T *dst_last, TrueType) {
  size_t n = static_cast<size_t>(last - first);
  if (n)
    ::memmove(static_cast<void *>(dst_last - n),
              static_cast<const void *>(first), n * sizeof(T));
}
template <typename T>
inline void MoveAssignBackward(T *first, T *last, T *dst_last, FalseType) {
  while (last != first)
    *--dst_last = Move(*--last);
}
template <typename T>
inline void MoveAssignBackward(T *first, T *last, T *dst_last) {
  MoveAssignBackward(first, last, dst_last,
                     IntegralConstant<bool, IsTriviallyCopyable<T>::value>());
}

// Assign [first, last) onto the live elements starting at dst, front to back
// (used to close a gap for erase).
template <typename T>
inline void MoveAssignForward(T *first, T *last, T *dst, TrueType) {
  size_t n = static_cast<size_t>(last - first);
  if (n)
    ::memmove(static_cast<void *>(dst), static_cast<const void *>(first),
              n * sizeof(T));
}
template <typename T>
inline void MoveAssignForward(T *first, T *last, T *dst, FalseType) {
  while (first != last)
    *dst++ = Move(*first++);
}
template <typename T> inline void MoveAssignForward(T *first, T *last, T *dst) {
  MoveAssignForward(first, last, dst,
                    IntegralConstant<bool, IsTriviallyCopyable<T>::value>());
}

} // namespace detail

} // namespace vapor
