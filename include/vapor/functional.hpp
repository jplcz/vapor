#pragma once
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/utility.hpp>

namespace vapor {

template <typename Sig> class Function;

template <typename R, typename... Args>
class VAPOR_NODISCARD Function<R(Args...)> {
public:
  static constexpr inline size_t SOO_SIZE = 32; // Adjustable inline buffer

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
      return Unexpected(CommonError::UnsupportedOperation);
    }

    // Attempt the fallible allocation part
    auto res = m_vtable->try_clone(&m_storage, *m_alloc);
    if (!res)
      return Unexpected(res.error());

    Function cloned;
    cloned.m_alloc = m_alloc;
    cloned.m_vtable = m_vtable;

    if (res.value() == (void *)0x1) {
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

  template <typename F>
  static Expected<Function, CommonError> Allocate(R (*func)(Args...),
                                                  Allocator &alloc) noexcept {
    Function f;
    f.m_alloc = &alloc;
    f.m_storage.func_ptr =
        reinterpret_cast<void *>(static_cast<R (*)(Args...)>(func));
    f.m_vtable =
        &c_func_vtable_factory<R (*)(Args...), R, Args...>::c_func_vtable;
    return f;
  }

  template <typename Callable>
  static Expected<Function, CommonError> Allocate(Callable func,
                                                  Allocator &alloc) noexcept {
    Function f;
    f.m_alloc = &alloc;
    if constexpr (sizeof(Callable) <= SOO_SIZE &&
                  alignof(Callable) <= alignof(storage)) {
      // Fits in SOO buffer
      new (&f.m_storage.buffer) Callable(Move(func));
      f.m_vtable = &function_vtable_factory<Callable, R, Args...>::soo_instance;
    } else {
      // Requires heap allocation
      auto res = alloc.Allocate(sizeof(Callable), alignof(Callable));
      if (!res)
        return Unexpected(CommonError::OutOfMemory);

      new (res) Callable(Move(func));

      f.m_storage.heap_ptr = res;
      f.m_vtable =
          &function_vtable_factory<Callable, R, Args...>::heap_instance;
    }
    return f;
  }

  template <typename F>
  static Expected<Function, CommonError> Create(F func) noexcept {
    return Allocate(Forward<F>(func), DefaultAllocator::GetDefault());
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
    void *func_ptr;
  };

  storage m_storage;

  struct vtable {
    R (*invoke)(const storage *, Args...);
    void (*destroy)(storage *, Allocator &);
    void (*move_and_destroy)(storage *src, storage *dest);
    Expected<void *, CommonError> (*try_clone)(const storage *, Allocator &);
    void (*copy_soo)(const storage *src, storage *dest);
  };

  template <typename F, typename R1, typename... Args1>
  struct c_func_vtable_factory {
    static constexpr vtable c_func_vtable = {
        [](const storage *s, Args... args) -> R {
          // Direct call: No indirection through a captured objectx
          auto fp = reinterpret_cast<R (*)(Args...)>(s->func_ptr);
          return fp(Forward<Args1>(args)...);
        },
        [](storage *, Allocator &) { /* No-op */ },
        [](storage *src, storage *dest) { dest->func_ptr = src->func_ptr; },
        [](const auto *, Allocator &) -> Expected<void *, CommonError> {
          return (void *)0x1UL; // Raw pointers don't need allocation
        },
        [](const storage *src, storage *dest) {
          // For C pointers, "cloning" is just a bitwise copy of the pointer
          dest->func_ptr = src->func_ptr;
        },
    };
  };

  template <typename F, typename R1, typename... Args1>
  struct function_vtable_factory {
    static constexpr typename Function<R1(Args1...)>::vtable soo_instance = {
        [](const storage *s, Args1... args) -> R {
          return (*reinterpret_cast<const F *>(s->buffer))(
              Forward<Args>(args)...);
        },
        [](storage *s, Allocator &) { reinterpret_cast<F *>(s->buffer)->~F(); },
        [](storage *src, auto *dest) {
          new (dest->buffer)
              F(Move(*reinterpret_cast<F *>(src->buffer)));
          reinterpret_cast<F *>(src->buffer)->~F();
        },
        [](const storage *s, Allocator &) -> Expected<void *, CommonError> {
          // No allocation needed for SOO, but we return a "fake" pointer
          // to signal that the data is ready for the SOO buffer
          return (void *)0x1UL;
        },
        [](const storage *src, storage *dest) {
          const F &src_obj = *reinterpret_cast<const F *>(src->buffer);
          // Guaranteed noexcept by our try_clone constraints
          new (dest->buffer) F(src_obj);
        },
    };

    static constexpr typename Function<R1(Args1...)>::vtable heap_instance = {
        [](const storage *s, Args1... args) -> R {
          return (*static_cast<const F *>(s->heap_ptr))(
              Forward<Args1>(args)...);
        },
        [](storage *s, Allocator &alloc) {
          static_cast<F *>(s->heap_ptr)->~F();
          alloc.Deallocate(s->heap_ptr, sizeof(F));
        },
        [](auto *src, auto *dest) {
          dest->heap_ptr = src->heap_ptr;
          src->heap_ptr = nullptr;
        },
        [](const storage *s,
           Allocator &alloc) -> Expected<void *, CommonError> {
          const F &original = *static_cast<const F *>(s->heap_ptr);
          auto res = alloc.Allocate(sizeof(F), alignof(F));
          if (!res)
            return Unexpected(CommonError::OutOfMemory);
          return new (res) F(original);
        },
        nullptr,
    };
  };

  const vtable *m_vtable = nullptr;
  Allocator *m_alloc = nullptr;
};

} // namespace vapor
