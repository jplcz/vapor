#pragma once
#include <assert.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/placement_new.hpp>
#include <vapor/expected.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

namespace vapor {

template <typename Sig> class Function;

template <typename R, typename... Args>
class VAPOR_NODISCARD_TYPE Function<R(Args...)> {
public:
  static constexpr size_t SOO_SIZE = 32; // Adjustable inline buffer

  Function() noexcept = default;

  Function(const Function &) = delete;
  Function &operator=(const Function &) = delete;

  Function(Function &&other) noexcept
      : m_vtable(other.m_vtable), m_alloc(other.m_alloc) {
    if (m_vtable) {
      m_vtable->move_and_destroy(&other.m_storage, &m_storage);
    }

    other.m_vtable = nullptr;
    other.m_alloc = nullptr;
  }

  Function &operator=(Function &&other) noexcept {
    if (this != &other) {
      if (m_vtable) {
        m_vtable->destroy(&m_storage, *m_alloc);
      }

      m_vtable = other.m_vtable;
      m_alloc = other.m_alloc;

      if (m_vtable) {
        m_vtable->move_and_destroy(&other.m_storage, &m_storage);
      }

      other.m_vtable = nullptr;
      other.m_alloc = nullptr;
    }
    return *this;
  }

  VAPOR_NODISCARD Expected<Function, CommonError> Clone() const & noexcept {
    if (!m_vtable || !m_vtable->try_clone) {
      return MakeUnexpected(CommonError::UnsupportedOperation);
    }

    // Attempt the fallible allocation part
    auto res = m_vtable->try_clone(&m_storage, *m_alloc);
    if (!res)
      return MakeUnexpected(res.error());

    Function cloned;
    cloned.m_alloc = m_alloc;
    cloned.m_vtable = m_vtable;

    if (res.value() == InlineMarker()) {
      assert(m_vtable->copy_soo != nullptr);
      m_vtable->copy_soo(&m_storage, &cloned.m_storage);
    } else {
      cloned.m_storage.heap_ptr = res.value();
    }

    return cloned;
  }

  VAPOR_NODISCARD Expected<Function, CommonError> Clone() && noexcept {
    // The object is a temporary! Instead of deep copying,
    // we can cheaply cast ourselves to a mutable rvalue and move our data out.
    return Function(Move(*this));
  }

  R operator()(Args &&...args) const noexcept {
    assert(m_vtable != nullptr);
    return m_vtable->invoke(&m_storage, Forward<Args>(args)...);
  }

  static Expected<Function, CommonError> Allocate(R (*func)(Args...),
                                                  Allocator &alloc) noexcept {
    Function f;
    f.m_alloc = &alloc;
    f.m_storage.fn = func;
    f.m_vtable = CFuncOps::Table();
    return f;
  }

  template <typename Callable>
  static Expected<Function, CommonError> Allocate(Callable func,
                                                  Allocator &alloc) noexcept {
    Function f;
    f.m_alloc = &alloc;
    typedef IntegralConstant<bool, (sizeof(Callable) <= SOO_SIZE &&
                                    alignof(Callable) <= alignof(storage))>
        FitsInline;
    if (!f.Store(Move(func), alloc, FitsInline()))
      return MakeUnexpected(CommonError::OutOfMemory);
    return f;
  }

  template <typename F>
  static Expected<Function, CommonError> Create(F func) noexcept {
    return Allocate(Move(func), DefaultAllocator::GetDefault());
  }

  ~Function() noexcept {
    if (m_vtable)
      m_vtable->destroy(&m_storage, *m_alloc);
  }

private:
  union storage {
    max_align_t align_buffer;
    char buffer[SOO_SIZE];
    void *heap_ptr;
    R (*fn)(Args...);
  };

  storage m_storage;

  struct vtable {
    R (*invoke)(const storage *, Args...);
    void (*destroy)(storage *, Allocator &);
    void (*move_and_destroy)(storage *src, storage *dest);
    Expected<void *, CommonError> (*try_clone)(const storage *, Allocator &);
    void (*copy_soo)(const storage *src, storage *dest);
  };

  // try_clone returns this marker when the copy fits in the inline buffer.
  static void *InlineMarker() noexcept { return reinterpret_cast<void *>(0x1); }

  // Plain function pointer: nothing to allocate, copy or destroy.
  struct CFuncOps {
    static R Invoke(const storage *s, Args... args) {
      return s->fn(Forward<Args>(args)...);
    }
    static void Destroy(storage *, Allocator &) {}
    static void MoveOut(storage *src, storage *dest) { dest->fn = src->fn; }
    static Expected<void *, CommonError> TryClone(const storage *,
                                                  Allocator &) {
      return InlineMarker();
    }
    static void Copy(const storage *src, storage *dest) { dest->fn = src->fn; }
    static const vtable *Table() {
      static const vtable t = {&Invoke, &Destroy, &MoveOut, &TryClone, &Copy};
      return &t;
    }
  };

  // Callable stored inside the inline buffer.
  template <typename F> struct SooOps {
    static R Invoke(const storage *s, Args... args) {
      return (*reinterpret_cast<const F *>(s->buffer))(Forward<Args>(args)...);
    }
    static void Destroy(storage *s, Allocator &) {
      reinterpret_cast<F *>(s->buffer)->~F();
    }
    static void MoveOut(storage *src, storage *dest) {
      new (dest->buffer) F(Move(*reinterpret_cast<F *>(src->buffer)));
      reinterpret_cast<F *>(src->buffer)->~F();
    }
    static Expected<void *, CommonError> TryClone(const storage *,
                                                  Allocator &) {
      return InlineMarker();
    }
    static void Copy(const storage *src, storage *dest) {
      new (dest->buffer) F(*reinterpret_cast<const F *>(src->buffer));
    }
    static const vtable *Table() {
      static const vtable t = {&Invoke, &Destroy, &MoveOut, &TryClone, &Copy};
      return &t;
    }
  };

  // Callable stored on the heap through the allocator.
  template <typename F> struct HeapOps {
    static R Invoke(const storage *s, Args... args) {
      return (*static_cast<const F *>(s->heap_ptr))(Forward<Args>(args)...);
    }
    static void Destroy(storage *s, Allocator &alloc) {
      static_cast<F *>(s->heap_ptr)->~F();
      alloc.Deallocate(s->heap_ptr, sizeof(F));
    }
    static void MoveOut(storage *src, storage *dest) {
      dest->heap_ptr = src->heap_ptr;
      src->heap_ptr = nullptr;
    }
    static Expected<void *, CommonError> TryClone(const storage *s,
                                                  Allocator &alloc) {
      void *mem = alloc.Allocate(sizeof(F), alignof(F));
      if (!mem)
        return MakeUnexpected(CommonError::OutOfMemory);
      return static_cast<void *>(new (mem)
                                     F(*static_cast<const F *>(s->heap_ptr)));
    }
    static const vtable *Table() {
      static const vtable t = {&Invoke, &Destroy, &MoveOut, &TryClone, nullptr};
      return &t;
    }
  };

  template <typename F> bool Store(F &&func, Allocator &, TrueType) noexcept {
    typedef DecayT<F> C;
    new (m_storage.buffer) C(Forward<F>(func));
    m_vtable = SooOps<C>::Table();
    return true;
  }
  template <typename F>
  bool Store(F &&func, Allocator &alloc, FalseType) noexcept {
    typedef DecayT<F> C;
    void *mem = alloc.Allocate(sizeof(C), alignof(C));
    if (!mem)
      return false;
    new (mem) C(Forward<F>(func));
    m_storage.heap_ptr = mem;
    m_vtable = HeapOps<C>::Table();
    return true;
  }

  const vtable *m_vtable = nullptr;
  Allocator *m_alloc = nullptr;
};

} // namespace vapor
