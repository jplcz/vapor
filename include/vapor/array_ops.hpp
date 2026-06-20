#pragma once
#include <stddef.h>
#include <string.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/traits.hpp>

namespace vapor {

enum class ArrayOpsError { OutOfMemory };

struct ArrayOperations {
private:
  struct TrivialTag {};
  struct CloneableTag {};
  struct CopyableTag {};

  // Trivial bitwise relocation
  template <typename T>
  VAPOR_ALWAYS_INLINE static Expected<void, ArrayOpsError>
  RelocateImpl(Allocator &, T *src, T *dest, size_t count,
               TrivialTag) noexcept {
    if (count > 0) {
      ::memcpy(static_cast<void *>(dest), static_cast<const void *>(src),
               count * sizeof(T));
    }
    return {};
  }

  // Fallible Cloneable Relocation
  template <typename T>
  static Expected<void, ArrayOpsError> RelocateImpl(Allocator &alloc, T *src,
                                                    T *dest, size_t count,
                                                    CloneableTag) noexcept {
    for (size_t i = 0; i < count; ++i) {
      auto clone_item = src[i].Clone();
      if (!clone_item) {
        // Rollback: Destroy any elements successfully built in the destination
        // chunk so far
        for (size_t j = 0; j < i; ++j) {
          alloc.Destroy(&dest[j]);
        }
        return MakeUnexpected(ArrayOpsError::OutOfMemory);
      }
      alloc.Construct(
          &dest[i],
          static_cast<typename IsCloneable<T>::value_type &&>(*clone_item));
      alloc.Destroy(&src[i]);
    }
    return {};
  }

  // Standard Reference Casting
  template <typename T, typename ErrorType>
  static Expected<void, ErrorType> RelocateImpl(Allocator &alloc, T *src,
                                                T *dest, size_t count,
                                                CopyableTag) noexcept {
    for (size_t i = 0; i < count; ++i) {
      alloc.Construct(&dest[i], static_cast<T &&>(src[i]));
      alloc.Destroy(&src[i]);
    }
    return {};
  }

  // Standard Copy Construction Fallback
  template <typename T, typename ErrorType>
  static Expected<void, ErrorType> CloneImpl(Allocator &alloc, const T *src,
                                             T *dest, size_t count,
                                             CopyableTag) noexcept {
    for (size_t i = 0; i < count; ++i) {
      alloc.Construct(&dest[i], src[i]);
    }
    return {};
  }

  // Element-by-Element Fallible Cloning
  template <typename T, typename ErrorType>
  static Expected<void, ErrorType> CloneImpl(Allocator &alloc, const T *src,
                                             T *dest, size_t count,
                                             CloneableTag) noexcept {
    for (size_t i = 0; i < count; ++i) {
      auto clone_item = src[i].Clone();
      if (!clone_item) {
        // Rollback structural elements built up to this failure offset
        for (size_t j = 0; j < i; ++j) {
          alloc.Destroy(&dest[j]);
        }
        return MakeUnexpected(ArrayOpsError::OutOfMemory);
      }
      alloc.Construct(
          &dest[i],
          static_cast<typename IsCloneable<T>::value_type &&>(*clone_item));
    }
    return {};
  }

  // Selector utility to simplify dispatch selection
  template <bool, typename TrueType, typename> struct selector {
    typedef TrueType type;
  };

  template <typename TrueType, typename FalseType>
  struct selector<false, TrueType, FalseType> {
    typedef FalseType type;
  };

public:
  // --- Unified Relocate API ---
  // Transfers elements from a source array to a target layout, destroying
  // source elements in the process.
  template <typename T>
  VAPOR_NODISCARD static Expected<void, ArrayOpsError>
  Relocate(Allocator &alloc, T *src, T *dest, size_t count) noexcept {
    typedef
        typename selector<IsRelocatable<T>::value, TrivialTag,
                          typename selector<IsCloneable<T>::value, CloneableTag,
                                            CopyableTag>::type>::type Tag;

    return RelocateImpl<T>(alloc, src, dest, count, Tag{});
  }

  // --- Unified Clone API ---
  // Deep-copies elements from a read-only source array to a fresh target
  // layout.
  template <typename T>
  VAPOR_NODISCARD static Expected<void, ArrayOpsError>
  Clone(Allocator &alloc, const T *src, T *dest, size_t count) noexcept {
    // Hashing / Cloning treats Trivial arithmetic items via standard bit-copy
    // construction routing
    typedef typename selector<IsCloneable<T>::value, CloneableTag,
                              CopyableTag>::type Tag;
    return CloneImpl<T>(alloc, src, dest, count, Tag{});
  }
};

} // namespace vapor