#pragma once

#include <stdint.h>
#include <vapor/io/fd.hpp>

#if defined(VAPOR_IO_SINGLE_THREADED) || !VAPOR_HAS_POSIX_IO
#define VAPOR_IO_LOCKING 0
#else
#define VAPOR_IO_LOCKING 1
#include <pthread.h>
#include <time.h>
#endif

// Minimal mutex and condition variable used by IoScheduler. Not part of the
// public API. With -DVAPOR_IO_SINGLE_THREADED (or without POSIX threads) every
// operation compiles to nothing and the scheduler must be used from one
// thread only.

namespace vapor {
namespace detail {

class Mutex {
#if VAPOR_IO_LOCKING
  pthread_mutex_t m_mutex;
#endif
public:
  Mutex() noexcept {
#if VAPOR_IO_LOCKING
    pthread_mutex_init(&m_mutex, nullptr);
#endif
  }
  ~Mutex() {
#if VAPOR_IO_LOCKING
    pthread_mutex_destroy(&m_mutex);
#endif
  }
  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;

  void lock() noexcept {
#if VAPOR_IO_LOCKING
    pthread_mutex_lock(&m_mutex);
#endif
  }
  void unlock() noexcept {
#if VAPOR_IO_LOCKING
    pthread_mutex_unlock(&m_mutex);
#endif
  }

#if VAPOR_IO_LOCKING
  pthread_mutex_t *native() noexcept { return &m_mutex; }
#endif
};

class MutexLock {
  Mutex &m_m;

public:
  explicit MutexLock(Mutex &m) noexcept : m_m(m) { m_m.lock(); }
  ~MutexLock() { m_m.unlock(); }
  MutexLock(const MutexLock &) = delete;
  MutexLock &operator=(const MutexLock &) = delete;
};

class ConditionVariable {
#if VAPOR_IO_LOCKING
  pthread_cond_t m_cond;
#endif
public:
  ConditionVariable() noexcept {
#if VAPOR_IO_LOCKING
    pthread_cond_init(&m_cond, nullptr);
#endif
  }
  ~ConditionVariable() {
#if VAPOR_IO_LOCKING
    pthread_cond_destroy(&m_cond);
#endif
  }
  ConditionVariable(const ConditionVariable &) = delete;
  ConditionVariable &operator=(const ConditionVariable &) = delete;

  // `m` must be locked by the caller; it is unlocked while waiting. May wake
  // spuriously, so callers re-check their condition in a loop.
  void wait(Mutex &m) noexcept {
#if VAPOR_IO_LOCKING
    pthread_cond_wait(&m_cond, m.native());
#else
    (void)m;
#endif
  }
  // Like wait() but gives up after about `ns` nanoseconds.
  void wait_for_ns(Mutex &m, int64_t ns) noexcept {
#if VAPOR_IO_LOCKING
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t total = static_cast<int64_t>(ts.tv_nsec) + ns % 1000000000;
    ts.tv_sec += static_cast<time_t>(ns / 1000000000 + total / 1000000000);
    ts.tv_nsec = static_cast<long>(total % 1000000000);
    pthread_cond_timedwait(&m_cond, m.native(), &ts);
#else
    (void)m;
    (void)ns;
#endif
  }
  void notify_all() noexcept {
#if VAPOR_IO_LOCKING
    pthread_cond_broadcast(&m_cond);
#endif
  }
};

} // namespace detail
} // namespace vapor
