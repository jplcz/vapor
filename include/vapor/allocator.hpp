#pragma once
#include <stddef.h>
#include <vapor/common.hpp>

#if defined(VAPOR_USE_STL)
#include <new> // Standard placement new definition
#else
// Freestanding placement new signatures required by the compiler
inline void *operator new(size_t, void *__p) noexcept { return __p; }
inline void *operator new[](size_t, void *__p) noexcept { return __p; }
#endif

#if !defined(VAPOR_OVERRIDE_DEFAULT_ALLOCATOR)
#include <stdlib.h>
#endif

namespace vapor {

class Allocator {
public:
  virtual ~Allocator() = default;

  // Core Memory Allocation Interface
  VAPOR_NODISCARD virtual void *
  Allocate(size_t size, size_t alignment = alignof(max_align_t)) noexcept = 0;
  VAPOR_NODISCARD virtual void *
  Reallocate(void *ptr, size_t new_size,
             size_t alignment = alignof(max_align_t)) noexcept = 0;
  virtual void Deallocate(void *ptr) noexcept = 0;

  // Utility construct/destroy helpers to bypass standard layout restrictions
  template <typename T, typename... Args>
  void Construct(T *ptr, Args &&...args) {
    new (static_cast<void *>(ptr)) T(static_cast<Args &&>(args)...);
  }

  template <typename T> void Destroy(T *ptr) noexcept {
    if (ptr) {
      ptr->~T();
    }
  }
};

#if !defined(VAPOR_OVERRIDE_DEFAULT_ALLOCATOR)
class DefaultAllocator final : public Allocator {
public:
  DefaultAllocator() noexcept = default;
  ~DefaultAllocator() override = default;

  VAPOR_NODISCARD void *Allocate(size_t size,
                                 size_t /*alignment*/) noexcept override {
    if (size == 0)
      return nullptr;
    return ::malloc(size);
  }

  VAPOR_NODISCARD void *Reallocate(void *ptr, size_t new_size,
                                   size_t /*alignment*/) noexcept override {
    if (new_size == 0) {
      ::free(ptr);
      return nullptr;
    }
    return ::realloc(ptr, new_size);
  }

  void Deallocate(void *ptr) noexcept override { ::free(ptr); }

  static DefaultAllocator &GetDefault() noexcept {
    static DefaultAllocator instance;
    return instance;
  }
};
#else
class DefaultAllocator : public Allocator {
public:
  static DefaultAllocator &GetDefault() noexcept;
};
#endif

} // namespace vapor
