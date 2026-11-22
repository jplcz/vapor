#pragma once
#include <assert.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>

namespace vapor {

// Move-only owner of a single heap object, modelled on std::unique_ptr.
//
// Differences from std::unique_ptr, all because the library has no exceptions
// and no global `new`:
//  * objects are created with TryMakeUnique / TryAllocateUnique, which return
//    Expected (OutOfMemory) instead of throwing;
//  * memory comes from, and is returned to, a vapor::Allocator that the
//    pointer remembers (no deleter template parameter).
//
// Converting a UniquePtr<Derived> to UniquePtr<Base> works and destroys and
// frees the original Derived object correctly, even if Base has no virtual
// destructor (the pointer remembers how to delete the most derived type).
template <typename T> class UniquePtr {
  template <typename U> friend class UniquePtr;

  // Destroys and frees the *most derived* object. m_block is its original
  // address (a Base* may differ from it under multiple inheritance).
  typedef void (*DeleteFn)(Allocator *, void *);

  Allocator *m_alloc;
  T *m_ptr;
  void *m_block;
  DeleteFn m_delete;

  template <typename D> static void DeleteAs(Allocator *a, void *p) {
    D *d = static_cast<D *>(p);
    d->~D();
    a->Deallocate(d, sizeof(D));
  }

  UniquePtr(Allocator *a, T *p, void *block, DeleteFn del) noexcept
      : m_alloc(a), m_ptr(p), m_block(block), m_delete(del) {}

public:
  typedef T element_type;
  typedef T *pointer;

  constexpr UniquePtr() noexcept
      : m_alloc(nullptr), m_ptr(nullptr), m_block(nullptr),
        m_delete(nullptr) {}
  constexpr UniquePtr(decltype(nullptr)) noexcept : UniquePtr() {}

  // Takes ownership of `p`, which must have been constructed in memory of
  // sizeof(T) bytes obtained from `alloc`. Prefer TryAllocateUnique.
  static UniquePtr Adopt(Allocator &alloc, T *p) noexcept {
    return UniquePtr(&alloc, p, p, p ? &DeleteAs<T> : nullptr);
  }

  UniquePtr(const UniquePtr &) = delete;
  UniquePtr &operator=(const UniquePtr &) = delete;

  UniquePtr(UniquePtr &&o) noexcept
      : m_alloc(o.m_alloc), m_ptr(o.m_ptr), m_block(o.m_block),
        m_delete(o.m_delete) {
    o.m_ptr = nullptr;
    o.m_block = nullptr;
    o.m_delete = nullptr;
  }

  template <typename U,
            typename = EnableIfT<!IsSame<U, T>::value &&
                                 IsConvertible<U *, T *>::value>>
  UniquePtr(UniquePtr<U> &&o) noexcept
      : m_alloc(o.m_alloc), m_ptr(o.m_ptr), m_block(o.m_block),
        m_delete(o.m_delete) {
    o.m_ptr = nullptr;
    o.m_block = nullptr;
    o.m_delete = nullptr;
  }

  UniquePtr &operator=(UniquePtr &&o) noexcept {
    if (this != &o) {
      reset();
      m_alloc = o.m_alloc;
      m_ptr = o.m_ptr;
      m_block = o.m_block;
      m_delete = o.m_delete;
      o.m_ptr = nullptr;
      o.m_block = nullptr;
      o.m_delete = nullptr;
    }
    return *this;
  }

  UniquePtr &operator=(decltype(nullptr)) noexcept {
    reset();
    return *this;
  }

  ~UniquePtr() { reset(); }

  VAPOR_NODISCARD T *get() const noexcept { return m_ptr; }
  VAPOR_NODISCARD explicit operator bool() const noexcept {
    return m_ptr != nullptr;
  }
  VAPOR_NODISCARD T &operator*() const noexcept {
    assert(m_ptr);
    return *m_ptr;
  }
  VAPOR_NODISCARD T *operator->() const noexcept {
    assert(m_ptr);
    return m_ptr;
  }

  // Non-trapping access (vapor extension)
  VAPOR_NODISCARD Expected<T *, CommonError> try_get() const noexcept {
    if (!m_ptr)
      return MakeUnexpected(CommonError::EmptyPointer);
    return m_ptr;
  }

  VAPOR_NODISCARD Allocator *get_allocator() const noexcept { return m_alloc; }

  // Gives up ownership without destroying the object. The caller becomes
  // responsible for destroying it and returning its memory to the allocator.
  VAPOR_NODISCARD T *release() noexcept {
    T *p = m_ptr;
    m_ptr = nullptr;
    m_block = nullptr;
    m_delete = nullptr;
    return p;
  }

  void reset() noexcept {
    if (m_ptr) {
      m_delete(m_alloc, m_block);
      m_ptr = nullptr;
      m_block = nullptr;
      m_delete = nullptr;
    }
  }

  void swap(UniquePtr &o) noexcept {
    UniquePtr tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }
};

// Array form: owns `count` default-constructed elements.
template <typename T> class UniquePtr<T[]> {
  Allocator *m_alloc;
  T *m_ptr;
  size_t m_count;

public:
  typedef T element_type;
  typedef T *pointer;

  constexpr UniquePtr() noexcept
      : m_alloc(nullptr), m_ptr(nullptr), m_count(0) {}
  constexpr UniquePtr(decltype(nullptr)) noexcept : UniquePtr() {}

  // `p` must point to `count` constructed elements in memory of
  // count * sizeof(T) bytes obtained from `alloc`.
  static UniquePtr Adopt(Allocator &alloc, T *p, size_t count) noexcept {
    UniquePtr r;
    r.m_alloc = &alloc;
    r.m_ptr = p;
    r.m_count = p ? count : 0;
    return r;
  }

  UniquePtr(const UniquePtr &) = delete;
  UniquePtr &operator=(const UniquePtr &) = delete;

  UniquePtr(UniquePtr &&o) noexcept
      : m_alloc(o.m_alloc), m_ptr(o.m_ptr), m_count(o.m_count) {
    o.m_ptr = nullptr;
    o.m_count = 0;
  }

  UniquePtr &operator=(UniquePtr &&o) noexcept {
    if (this != &o) {
      reset();
      m_alloc = o.m_alloc;
      m_ptr = o.m_ptr;
      m_count = o.m_count;
      o.m_ptr = nullptr;
      o.m_count = 0;
    }
    return *this;
  }

  ~UniquePtr() { reset(); }

  VAPOR_NODISCARD T *get() const noexcept { return m_ptr; }
  VAPOR_NODISCARD size_t size() const noexcept { return m_count; }
  VAPOR_NODISCARD explicit operator bool() const noexcept {
    return m_ptr != nullptr;
  }
  VAPOR_NODISCARD T &operator[](size_t i) const noexcept {
    assert(i < m_count);
    return m_ptr[i];
  }

  VAPOR_NODISCARD T *release() noexcept {
    T *p = m_ptr;
    m_ptr = nullptr;
    m_count = 0;
    return p;
  }

  void reset() noexcept {
    if (m_ptr) {
      detail::DestroyRange(m_ptr, m_count);
      m_alloc->Deallocate(m_ptr, m_count * sizeof(T));
      m_ptr = nullptr;
      m_count = 0;
    }
  }

  void swap(UniquePtr &o) noexcept {
    UniquePtr tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }
};

// std::make_unique, but allocator-backed and fallible.
template <typename T, typename... Args>
VAPOR_NODISCARD Expected<UniquePtr<T>, CommonError>
TryAllocateUnique(Allocator &alloc, Args &&...args) {
  void *mem = alloc.Allocate(sizeof(T), alignof(T));
  if (!mem)
    return MakeUnexpected(CommonError::OutOfMemory);
  T *obj = detail::ConstructAt<T>(mem, Forward<Args>(args)...);
  return UniquePtr<T>::Adopt(alloc, obj);
}

template <typename T, typename... Args>
VAPOR_NODISCARD Expected<UniquePtr<T>, CommonError>
TryMakeUnique(Args &&...args) {
  return TryAllocateUnique<T>(DefaultAllocator::GetDefault(),
                              Forward<Args>(args)...);
}

// std::make_unique<T[]>(n): n value-initialised elements.
template <typename T>
VAPOR_NODISCARD Expected<UniquePtr<T[]>, CommonError>
TryAllocateUniqueArray(Allocator &alloc, size_t count) {
  if (count == 0)
    return UniquePtr<T[]>();
  if (count > VAPOR_SIZE_MAX / sizeof(T))
    return MakeUnexpected(CommonError::OutOfMemory);
  void *mem = alloc.Allocate(count * sizeof(T), alignof(T));
  if (!mem)
    return MakeUnexpected(CommonError::OutOfMemory);
  T *first = static_cast<T *>(mem);
  for (size_t i = 0; i < count; ++i)
    detail::ConstructAt<T>(first + i);
  return UniquePtr<T[]>::Adopt(alloc, first, count);
}

template <typename T>
VAPOR_NODISCARD Expected<UniquePtr<T[]>, CommonError>
TryMakeUniqueArray(size_t count) {
  return TryAllocateUniqueArray<T>(DefaultAllocator::GetDefault(), count);
}

template <typename T>
VAPOR_NODISCARD bool operator==(const UniquePtr<T> &a,
                                const UniquePtr<T> &b) noexcept {
  return a.get() == b.get();
}
template <typename T>
VAPOR_NODISCARD bool operator!=(const UniquePtr<T> &a,
                                const UniquePtr<T> &b) noexcept {
  return a.get() != b.get();
}
template <typename T>
VAPOR_NODISCARD bool operator==(const UniquePtr<T> &a,
                                decltype(nullptr)) noexcept {
  return a.get() == nullptr;
}
template <typename T>
VAPOR_NODISCARD bool operator!=(const UniquePtr<T> &a,
                                decltype(nullptr)) noexcept {
  return a.get() != nullptr;
}

} // namespace vapor
