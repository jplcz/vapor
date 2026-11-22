#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vapor/config.hpp>
#include <vapor/type_traits.hpp>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

// Atomic<T>: a variable that several threads can read and write safely.
//
// Why: if two threads touch the same plain int, the result is a data race
// (undefined behaviour). Atomic operations are indivisible, so counters,
// flags and lock-free structures can be built on them. This is the STL-free
// equivalent of std::atomic, using compiler builtins (GCC/Clang `__atomic_*`,
// MSVC `_Interlocked*`). The MSVC path is a best-effort port: it always uses
// full (sequentially consistent) ordering and supports 4- and 8-byte types.
//
// How:
//   Atomic<int> counter(0);
//   counter.fetch_add(1);                 // or ++counter
//   int seen = counter.load();
//   int expected = 5;
//   counter.compare_exchange_strong(expected, 6);  // set to 6 only if it is 5
//
// MemoryOrder tells the CPU/compiler how much surrounding memory access may
// be reordered around the operation. When unsure keep the default, SeqCst
// (sequentially consistent): the strongest and easiest to reason about.
// Relaxed: atomic only, no ordering (fine for plain counters).
// Acquire/Release: pair a Release store with an Acquire load to hand data
// from one thread to another.
//
// T must be trivially copyable with size 1, 2, 4 or 8. Integers get fetch_add/
// sub/and/or/xor; pointers get fetch_add/sub (scaled by the element size).
// Atomic objects cannot be copied.

namespace vapor {

enum class MemoryOrder : int {
  Relaxed = 0,
  Consume = 1,
  Acquire = 2,
  Release = 3,
  AcqRel = 4,
  SeqCst = 5
};

namespace detail {

#if defined(__GNUC__) || defined(__clang__)

inline int ToBuiltinOrder(MemoryOrder o) { return static_cast<int>(o); }

template <typename T> T AtomicLoad(const T *p, MemoryOrder o) {
  T r;
  __atomic_load(p, &r, ToBuiltinOrder(o));
  return r;
}
template <typename T> void AtomicStore(T *p, T v, MemoryOrder o) {
  __atomic_store(p, &v, ToBuiltinOrder(o));
}
template <typename T> T AtomicExchange(T *p, T v, MemoryOrder o) {
  T r;
  __atomic_exchange(p, &v, &r, ToBuiltinOrder(o));
  return r;
}
template <typename T>
bool AtomicCas(T *p, T &expected, T desired, bool weak, MemoryOrder ok,
               MemoryOrder fail) {
  return __atomic_compare_exchange(p, &expected, &desired, weak,
                                   ToBuiltinOrder(ok), ToBuiltinOrder(fail));
}
template <typename T> T AtomicFetchAdd(T *p, T v, MemoryOrder o) {
  return __atomic_fetch_add(p, v, ToBuiltinOrder(o));
}
template <typename T> T AtomicFetchSub(T *p, T v, MemoryOrder o) {
  return __atomic_fetch_sub(p, v, ToBuiltinOrder(o));
}
template <typename T> T AtomicFetchAnd(T *p, T v, MemoryOrder o) {
  return __atomic_fetch_and(p, v, ToBuiltinOrder(o));
}
template <typename T> T AtomicFetchOr(T *p, T v, MemoryOrder o) {
  return __atomic_fetch_or(p, v, ToBuiltinOrder(o));
}
template <typename T> T AtomicFetchXor(T *p, T v, MemoryOrder o) {
  return __atomic_fetch_xor(p, v, ToBuiltinOrder(o));
}
template <size_t N> struct AtomicLockFree {
  static constexpr bool value = __atomic_always_lock_free(N, 0);
};

#else // MSVC: 4- and 8-byte types through compare-exchange

template <size_t N> struct AtomicRaw;
template <> struct AtomicRaw<4> {
  typedef long type;
  static type Cas(volatile type *p, type desired, type expected) {
    return _InterlockedCompareExchange(p, desired, expected);
  }
  static type Xchg(volatile type *p, type v) {
    return _InterlockedExchange(p, v);
  }
};
template <> struct AtomicRaw<8> {
  typedef __int64 type;
  static type Cas(volatile type *p, type desired, type expected) {
    return _InterlockedCompareExchange64(p, desired, expected);
  }
  static type Xchg(volatile type *p, type v) {
    return _InterlockedExchange64(p, v);
  }
};
template <typename T> typename AtomicRaw<sizeof(T)>::type ToRaw(T v) {
  typename AtomicRaw<sizeof(T)>::type r;
  memcpy(&r, &v, sizeof(T));
  return r;
}
template <typename T> T FromRaw(typename AtomicRaw<sizeof(T)>::type r) {
  T v;
  memcpy(&v, &r, sizeof(T));
  return v;
}
template <typename T> T AtomicLoad(const T *p, MemoryOrder) {
  typedef AtomicRaw<sizeof(T)> R;
  return FromRaw<T>(R::Cas(const_cast<volatile typename R::type *>(
                               reinterpret_cast<const typename R::type *>(p)),
                           0, 0));
}
template <typename T> void AtomicStore(T *p, T v, MemoryOrder) {
  typedef AtomicRaw<sizeof(T)> R;
  R::Xchg(reinterpret_cast<volatile typename R::type *>(p), ToRaw(v));
}
template <typename T> T AtomicExchange(T *p, T v, MemoryOrder) {
  typedef AtomicRaw<sizeof(T)> R;
  return FromRaw<T>(
      R::Xchg(reinterpret_cast<volatile typename R::type *>(p), ToRaw(v)));
}
template <typename T>
bool AtomicCas(T *p, T &expected, T desired, bool, MemoryOrder, MemoryOrder) {
  typedef AtomicRaw<sizeof(T)> R;
  typename R::type e = ToRaw(expected);
  typename R::type prev = R::Cas(
      reinterpret_cast<volatile typename R::type *>(p), ToRaw(desired), e);
  if (prev == e)
    return true;
  expected = FromRaw<T>(prev);
  return false;
}
#define VAPOR_ATOMIC_RMW(Name, expr)                                           \
  template <typename T> T Name(T *p, T v, MemoryOrder o) {                     \
    T cur = AtomicLoad(p, o);                                                  \
    while (!AtomicCas(p, cur, static_cast<T>(expr), false, o, o)) {            \
    }                                                                          \
    return cur;                                                                \
  }
VAPOR_ATOMIC_RMW(AtomicFetchAdd, cur + v)
VAPOR_ATOMIC_RMW(AtomicFetchSub, cur - v)
VAPOR_ATOMIC_RMW(AtomicFetchAnd, cur &v)
VAPOR_ATOMIC_RMW(AtomicFetchOr, cur | v)
VAPOR_ATOMIC_RMW(AtomicFetchXor, cur ^ v)
#undef VAPOR_ATOMIC_RMW
template <size_t N> struct AtomicLockFree {
  static constexpr bool value = N == 4 || N == 8;
};

#endif

inline MemoryOrder FailureOrder(MemoryOrder o) {
  return o == MemoryOrder::AcqRel    ? MemoryOrder::Acquire
         : o == MemoryOrder::Release ? MemoryOrder::Relaxed
                                     : o;
}

// 0 = any trivially copyable value, 1 = integer, 2 = pointer
template <typename T>
struct AtomicKind : IntegralConstant<int, IsPointer<T>::value ? 2
                                          : (IsIntegral<T>::value &&
                                             !IsSame<RemoveCVT<T>, bool>::value)
                                              ? 1
                                              : 0> {};

template <typename T, int Kind = AtomicKind<T>::value> class AtomicImpl {
public:
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                    sizeof(T) == 8,
                "Atomic<T> supports sizes 1, 2, 4 and 8");
  static_assert(IsTriviallyCopyable<T>::value,
                "Atomic<T> requires a trivially copyable type");

  static constexpr bool is_always_lock_free = AtomicLockFree<sizeof(T)>::value;

  AtomicImpl() noexcept = default;
  constexpr AtomicImpl(T v) noexcept : v_(v) {}
  AtomicImpl(const AtomicImpl &) = delete;
  AtomicImpl &operator=(const AtomicImpl &) = delete;

  VAPOR_NODISCARD T load(MemoryOrder o = MemoryOrder::SeqCst) const noexcept {
    return AtomicLoad(&v_, o);
  }
  void store(T v, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    AtomicStore(&v_, v, o);
  }
  T exchange(T v, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicExchange(&v_, v, o);
  }
  // On failure `expected` is updated with the value that was found.
  bool compare_exchange_strong(T &expected, T desired,
                               MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicCas(&v_, expected, desired, false, o, FailureOrder(o));
  }
  bool compare_exchange_strong(T &expected, T desired, MemoryOrder ok,
                               MemoryOrder fail) noexcept {
    return AtomicCas(&v_, expected, desired, false, ok, fail);
  }
  // May fail spuriously even if equal; use inside a retry loop.
  bool compare_exchange_weak(T &expected, T desired,
                             MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicCas(&v_, expected, desired, true, o, FailureOrder(o));
  }
  bool compare_exchange_weak(T &expected, T desired, MemoryOrder ok,
                             MemoryOrder fail) noexcept {
    return AtomicCas(&v_, expected, desired, true, ok, fail);
  }

  operator T() const noexcept { return load(); }
  T operator=(T v) noexcept {
    store(v);
    return v;
  }

protected:
  alignas(sizeof(T)) T v_;
};

template <typename T, int Kind>
constexpr bool AtomicImpl<T, Kind>::is_always_lock_free;

template <typename T> class AtomicImpl<T, 1> : public AtomicImpl<T, 0> {
  typedef AtomicImpl<T, 0> Base;

public:
  AtomicImpl() noexcept = default;
  constexpr AtomicImpl(T v) noexcept : Base(v) {}
  using Base::operator=;

  T fetch_add(T v, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicFetchAdd(&this->v_, v, o);
  }
  T fetch_sub(T v, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicFetchSub(&this->v_, v, o);
  }
  T fetch_and(T v, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicFetchAnd(&this->v_, v, o);
  }
  T fetch_or(T v, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicFetchOr(&this->v_, v, o);
  }
  T fetch_xor(T v, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return AtomicFetchXor(&this->v_, v, o);
  }
  T operator++() noexcept { return static_cast<T>(fetch_add(1) + 1); }
  T operator++(int) noexcept { return fetch_add(1); }
  T operator--() noexcept { return static_cast<T>(fetch_sub(1) - 1); }
  T operator--(int) noexcept { return fetch_sub(1); }
  T operator+=(T v) noexcept { return static_cast<T>(fetch_add(v) + v); }
  T operator-=(T v) noexcept { return static_cast<T>(fetch_sub(v) - v); }
  T operator&=(T v) noexcept { return static_cast<T>(fetch_and(v) & v); }
  T operator|=(T v) noexcept { return static_cast<T>(fetch_or(v) | v); }
  T operator^=(T v) noexcept { return static_cast<T>(fetch_xor(v) ^ v); }
};

template <typename T> class AtomicImpl<T, 2> : public AtomicImpl<T, 0> {
  typedef AtomicImpl<T, 0> Base;
  typedef typename RemovePointer<T>::type Elem;

public:
  AtomicImpl() noexcept = default;
  constexpr AtomicImpl(T v) noexcept : Base(v) {}
  using Base::operator=;

  T fetch_add(ptrdiff_t n, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return Offset(n, o, true);
  }
  T fetch_sub(ptrdiff_t n, MemoryOrder o = MemoryOrder::SeqCst) noexcept {
    return Offset(n, o, false);
  }
  T operator++() noexcept { return fetch_add(1) + 1; }
  T operator++(int) noexcept { return fetch_add(1); }
  T operator--() noexcept { return fetch_sub(1) - 1; }
  T operator--(int) noexcept { return fetch_sub(1); }
  T operator+=(ptrdiff_t n) noexcept { return fetch_add(n) + n; }
  T operator-=(ptrdiff_t n) noexcept { return fetch_sub(n) - n; }

private:
  // Done with a compare-exchange loop so the step is scaled by sizeof(Elem).
  T Offset(ptrdiff_t n, MemoryOrder o, bool add) noexcept {
    T cur = this->load(MemoryOrder::Relaxed);
    while (!this->compare_exchange_weak(cur, add ? cur + n : cur - n, o,
                                        FailureOrder(o))) {
    }
    return cur;
  }
};

} // namespace detail

template <typename T> class Atomic : public detail::AtomicImpl<T> {
  typedef detail::AtomicImpl<T> Base;

public:
  Atomic() noexcept = default;
  constexpr Atomic(T v) noexcept : Base(v) {}
  using Base::operator=;
};

// Orders memory accesses between threads without touching a variable.
inline void AtomicThreadFence(MemoryOrder o = MemoryOrder::SeqCst) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  __atomic_thread_fence(static_cast<int>(o));
#else
  (void)o;
  _ReadWriteBarrier();
  long x = 0;
  _InterlockedExchange(&x, 1);
#endif
}
// Orders accesses against a signal handler on the same thread.
inline void AtomicSignalFence(MemoryOrder o = MemoryOrder::SeqCst) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  __atomic_signal_fence(static_cast<int>(o));
#else
  (void)o;
  _ReadWriteBarrier();
#endif
}

} // namespace vapor
