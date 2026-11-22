#pragma once
#include <assert.h>
#include <stddef.h>
#include <vapor/allocator.hpp>
#include <vapor/atomic.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/hash.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// SharedPtr / WeakPtr: shared ownership of one heap object, modelled on
// std::shared_ptr and std::weak_ptr.
//
// Why: sometimes several owners need an object and none of them knows who
// finishes last (a cache entry used by many requests, a node referenced from
// several lists, a resource handed to worker threads). Reference counting
// destroys the object exactly when the last owner lets go, and WeakPtr lets
// you observe the object without keeping it alive, which breaks ownership
// cycles (a parent owns its children, children hold only a WeakPtr back).
// If there is a single owner, use UniquePtr instead: it is cheaper.
//
// How:
//
//   Expected<SharedPtr<Config>, CommonError> r = TryMakeShared<Config>(args);
//   if (!r) return r.error();                 // OutOfMemory
//   SharedPtr<Config> a = Move(r.value());
//   SharedPtr<Config> b = a;                  // second owner; use_count() == 2
//   WeakPtr<Config> w = a;                    // observer only
//   a.reset(); b.reset();                     // object destroyed here
//   SharedPtr<Config> c = w.lock();           // empty: the object is gone
//
// Differences from the std types, all because the library has no exceptions
// and no global `new`:
//  * objects are created with TryMakeShared / TryAllocateShared, which return
//    Expected (OutOfMemory) instead of throwing;
//  * the object and its reference counts share one allocation obtained from a
//    vapor::Allocator (like std::make_shared); there is no way to adopt a raw
//    pointer;
//  * there is no enable_shared_from_this and no dynamic cast.
//
// Thread safety: like std::shared_ptr, the reference counts are atomic, so
// different SharedPtr objects that share an object may be copied, reset and
// destroyed from different threads at the same time. Reading and writing the
// *same* SharedPtr variable from two threads needs your own locking, and the
// pointed-to object is not synchronised at all.

namespace vapor {

template <typename T> class SharedPtr;
template <typename T> class WeakPtr;

namespace detail {

// Counts shared by all owners of one object.
//   strong: number of SharedPtr owners; the object lives while it is > 0.
//   weak:   number of WeakPtr observers, plus one for "all the strong owners
//           together"; the memory block lives while it is > 0. Counting the
//           strong group once means the block is freed by whoever drops last,
//           without a race between the last SharedPtr and the last WeakPtr.
struct SharedControl {
  Atomic<size_t> strong;
  Atomic<size_t> weak;
  Allocator *alloc;
  void (*destroy_object)(SharedControl *);
  void (*free_block)(SharedControl *);

  SharedControl(Allocator *a, void (*d)(SharedControl *),
                void (*f)(SharedControl *)) noexcept
      : strong(1), weak(1), alloc(a), destroy_object(d), free_block(f) {}
};

template <typename T> struct SharedBlock : SharedControl {
  alignas(T) unsigned char storage[sizeof(T)];

  explicit SharedBlock(Allocator *a) noexcept
      : SharedControl(a, &DestroyObject, &FreeBlock) {}

  T *object() noexcept { return reinterpret_cast<T *>(storage); }

  static void DestroyObject(SharedControl *c) {
    static_cast<SharedBlock *>(c)->object()->~T();
  }
  static void FreeBlock(SharedControl *c) {
    Allocator *a = c->alloc;
    SharedBlock *b = static_cast<SharedBlock *>(c);
    b->~SharedBlock();
    a->Deallocate(b, sizeof(SharedBlock));
  }
};

inline void ReleaseWeak(SharedControl *c) noexcept {
  if (c->weak.fetch_sub(1, MemoryOrder::AcqRel) == 1)
    c->free_block(c);
}

inline void ReleaseStrong(SharedControl *c) noexcept {
  if (c->strong.fetch_sub(1, MemoryOrder::AcqRel) == 1) {
    c->destroy_object(c);
    ReleaseWeak(c);
  }
}

// Gives the creation functions access to SharedPtr's private constructor.
struct SharedAccess {
  template <typename T>
  static SharedPtr<T> Make(T *p, SharedControl *c) noexcept {
    return SharedPtr<T>(p, c);
  }
};

} // namespace detail

template <typename T> class SharedPtr {
  template <typename U> friend class SharedPtr;
  template <typename U> friend class WeakPtr;
  friend struct detail::SharedAccess;

  T *m_ptr;
  detail::SharedControl *m_ctrl;

  // Takes over one strong reference that the caller already counted.
  SharedPtr(T *p, detail::SharedControl *c) noexcept : m_ptr(p), m_ctrl(c) {}

public:
  typedef T element_type;

  constexpr SharedPtr() noexcept : m_ptr(nullptr), m_ctrl(nullptr) {}
  constexpr SharedPtr(decltype(nullptr)) noexcept : SharedPtr() {}

  SharedPtr(const SharedPtr &o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    if (m_ctrl)
      m_ctrl->strong.fetch_add(1, MemoryOrder::Relaxed);
  }

  SharedPtr(SharedPtr &&o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    o.m_ptr = nullptr;
    o.m_ctrl = nullptr;
  }

  template <typename U, typename = EnableIfT<IsConvertible<U *, T *>::value>>
  SharedPtr(const SharedPtr<U> &o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    if (m_ctrl)
      m_ctrl->strong.fetch_add(1, MemoryOrder::Relaxed);
  }

  template <typename U, typename = EnableIfT<IsConvertible<U *, T *>::value>>
  SharedPtr(SharedPtr<U> &&o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    o.m_ptr = nullptr;
    o.m_ctrl = nullptr;
  }

  // Aliasing constructor: shares ownership with `owner` but points at `p`
  // (typically a member of the owned object). `p` must stay valid as long as
  // `owner`'s object is alive.
  template <typename U>
  SharedPtr(const SharedPtr<U> &owner, T *p) noexcept
      : m_ptr(p), m_ctrl(owner.m_ctrl) {
    if (m_ctrl)
      m_ctrl->strong.fetch_add(1, MemoryOrder::Relaxed);
  }

  SharedPtr &operator=(const SharedPtr &o) noexcept {
    SharedPtr(o).swap(*this);
    return *this;
  }
  SharedPtr &operator=(SharedPtr &&o) noexcept {
    SharedPtr(Move(o)).swap(*this);
    return *this;
  }
  template <typename U, typename = EnableIfT<IsConvertible<U *, T *>::value>>
  SharedPtr &operator=(const SharedPtr<U> &o) noexcept {
    SharedPtr(o).swap(*this);
    return *this;
  }
  template <typename U, typename = EnableIfT<IsConvertible<U *, T *>::value>>
  SharedPtr &operator=(SharedPtr<U> &&o) noexcept {
    SharedPtr(Move(o)).swap(*this);
    return *this;
  }
  SharedPtr &operator=(decltype(nullptr)) noexcept {
    reset();
    return *this;
  }

  ~SharedPtr() {
    if (m_ctrl)
      detail::ReleaseStrong(m_ctrl);
  }

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

  // Non-trapping access (vapor extension).
  VAPOR_NODISCARD Expected<T *, CommonError> try_get() const noexcept {
    if (!m_ptr)
      return MakeUnexpected(CommonError::EmptyPointer);
    return m_ptr;
  }

  // Number of SharedPtr owners (0 when empty). Another thread may change it
  // right after the call, so only rely on it for diagnostics or when you know
  // no other thread touches the object.
  VAPOR_NODISCARD size_t use_count() const noexcept {
    return m_ctrl ? m_ctrl->strong.load(MemoryOrder::Relaxed) : 0;
  }
  VAPOR_NODISCARD bool unique() const noexcept { return use_count() == 1; }

  void reset() noexcept { SharedPtr().swap(*this); }

  void swap(SharedPtr &o) noexcept {
    T *p = m_ptr;
    m_ptr = o.m_ptr;
    o.m_ptr = p;
    detail::SharedControl *c = m_ctrl;
    m_ctrl = o.m_ctrl;
    o.m_ctrl = c;
  }
};

template <typename T> class WeakPtr {
  template <typename U> friend class WeakPtr;

  T *m_ptr;
  detail::SharedControl *m_ctrl;

  void AddRef() noexcept {
    if (m_ctrl)
      m_ctrl->weak.fetch_add(1, MemoryOrder::Relaxed);
  }

public:
  typedef T element_type;

  constexpr WeakPtr() noexcept : m_ptr(nullptr), m_ctrl(nullptr) {}

  WeakPtr(const WeakPtr &o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    AddRef();
  }
  WeakPtr(WeakPtr &&o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    o.m_ptr = nullptr;
    o.m_ctrl = nullptr;
  }
  template <typename U, typename = EnableIfT<IsConvertible<U *, T *>::value>>
  WeakPtr(const WeakPtr<U> &o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    AddRef();
  }
  template <typename U, typename = EnableIfT<IsConvertible<U *, T *>::value>>
  WeakPtr(const SharedPtr<U> &o) noexcept : m_ptr(o.m_ptr), m_ctrl(o.m_ctrl) {
    AddRef();
  }

  WeakPtr &operator=(const WeakPtr &o) noexcept {
    WeakPtr(o).swap(*this);
    return *this;
  }
  WeakPtr &operator=(WeakPtr &&o) noexcept {
    WeakPtr(Move(o)).swap(*this);
    return *this;
  }
  template <typename U, typename = EnableIfT<IsConvertible<U *, T *>::value>>
  WeakPtr &operator=(const SharedPtr<U> &o) noexcept {
    WeakPtr(o).swap(*this);
    return *this;
  }

  ~WeakPtr() {
    if (m_ctrl)
      detail::ReleaseWeak(m_ctrl);
  }

  // Number of SharedPtr owners that currently keep the object alive.
  VAPOR_NODISCARD size_t use_count() const noexcept {
    return m_ctrl ? m_ctrl->strong.load(MemoryOrder::Relaxed) : 0;
  }
  VAPOR_NODISCARD bool expired() const noexcept { return use_count() == 0; }

  // Returns a SharedPtr to the object if it is still alive, otherwise an empty
  // one. The check and the new reference are one atomic step, so the object
  // cannot be destroyed in between.
  VAPOR_NODISCARD SharedPtr<T> lock() const noexcept {
    if (!m_ctrl)
      return SharedPtr<T>();
    size_t n = m_ctrl->strong.load(MemoryOrder::Relaxed);
    do {
      if (n == 0)
        return SharedPtr<T>();
    } while (!m_ctrl->strong.compare_exchange_weak(
        n, n + 1, MemoryOrder::AcqRel, MemoryOrder::Relaxed));
    return detail::SharedAccess::Make<T>(m_ptr, m_ctrl);
  }

  void reset() noexcept { WeakPtr().swap(*this); }

  void swap(WeakPtr &o) noexcept {
    T *p = m_ptr;
    m_ptr = o.m_ptr;
    o.m_ptr = p;
    detail::SharedControl *c = m_ctrl;
    m_ctrl = o.m_ctrl;
    o.m_ctrl = c;
  }
};

// std::make_shared, but allocator-backed and fallible.
template <typename T, typename... Args>
VAPOR_NODISCARD Expected<SharedPtr<T>, CommonError>
TryAllocateShared(Allocator &alloc, Args &&...args) {
  typedef detail::SharedBlock<T> Block;
  void *mem = alloc.Allocate(sizeof(Block), alignof(Block));
  if (!mem)
    return MakeUnexpected(CommonError::OutOfMemory);
  Block *block = detail::ConstructAt<Block>(mem, &alloc);
  detail::ConstructAt<T>(block->storage, Forward<Args>(args)...);
  return detail::SharedAccess::Make<T>(block->object(), block);
}

template <typename T, typename... Args>
VAPOR_NODISCARD Expected<SharedPtr<T>, CommonError>
TryMakeShared(Args &&...args) {
  return TryAllocateShared<T>(DefaultAllocator::GetDefault(),
                              Forward<Args>(args)...);
}

// Pointer casts that keep sharing ownership (std::static_pointer_cast, ...).
template <typename T, typename U>
VAPOR_NODISCARD SharedPtr<T> StaticPointerCast(const SharedPtr<U> &p) noexcept {
  return SharedPtr<T>(p, static_cast<T *>(p.get()));
}
template <typename T, typename U>
VAPOR_NODISCARD SharedPtr<T> ConstPointerCast(const SharedPtr<U> &p) noexcept {
  return SharedPtr<T>(p, const_cast<T *>(p.get()));
}

template <typename T, typename U>
VAPOR_NODISCARD bool operator==(const SharedPtr<T> &a,
                                const SharedPtr<U> &b) noexcept {
  return a.get() == b.get();
}
template <typename T, typename U>
VAPOR_NODISCARD bool operator!=(const SharedPtr<T> &a,
                                const SharedPtr<U> &b) noexcept {
  return a.get() != b.get();
}
template <typename T>
VAPOR_NODISCARD bool operator==(const SharedPtr<T> &a,
                                decltype(nullptr)) noexcept {
  return a.get() == nullptr;
}
template <typename T>
VAPOR_NODISCARD bool operator!=(const SharedPtr<T> &a,
                                decltype(nullptr)) noexcept {
  return a.get() != nullptr;
}

template <typename T> struct Hash<SharedPtr<T>, void> {
  VAPOR_NODISCARD size_t operator()(const SharedPtr<T> &p) const noexcept {
    return Hash<T *>()(p.get());
  }
};

} // namespace vapor
