#pragma once
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <vapor/checked_math.hpp>
#include <vapor/common.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Time measurement: Duration, TimePoint, clocks and Stopwatch.
//
// Why: raw integers for time ("is 500 milliseconds or microseconds?") cause
// unit mix-ups, and the C clock APIs are awkward. A Duration carries its unit
// in its type, so `Duration::FromMilliseconds(500)` can never be mistaken for
// 500 seconds, and subtracting two TimePoints gives a Duration.
//
// How:
//   Stopwatch sw;                       // starts now, on the monotonic clock
//   DoWork();
//   int64_t ms = sw.Elapsed().milliseconds();
//
//   Duration timeout = Duration::FromSeconds(2) +
//   Duration::FromMilliseconds(5); TimePoint deadline = SteadyClock::Now() +
//   timeout; if (SteadyClock::Now() >= deadline) { /* timed out */ }
//
// SteadyClock never goes backwards (use it for measuring and timeouts).
// SystemClock is the wall-clock time since 1970-01-01 UTC and can jump when
// the user or NTP (Network Time Protocol) adjusts the clock (use it for
// timestamps shown to people). Durations store a signed 64-bit count of
// nanoseconds (about +-292 years); the From* factories saturate at the limits
// instead of overflowing. Nothing allocates or throws.

namespace vapor {

class Duration {
  int64_t m_ns;

  static Duration Scaled(int64_t value, int64_t ns_per_unit) noexcept {
    return Duration(SaturatingMul<int64_t>(value, ns_per_unit));
  }

public:
  constexpr Duration() noexcept : m_ns(0) {}
  constexpr explicit Duration(int64_t nanoseconds) noexcept
      : m_ns(nanoseconds) {}

  static Duration FromNanoseconds(int64_t v) noexcept { return Duration(v); }
  static Duration FromMicroseconds(int64_t v) noexcept {
    return Scaled(v, 1000);
  }
  static Duration FromMilliseconds(int64_t v) noexcept {
    return Scaled(v, 1000000);
  }
  static Duration FromSeconds(int64_t v) noexcept {
    return Scaled(v, 1000000000);
  }
  static Duration FromMinutes(int64_t v) noexcept {
    return Scaled(v, 60 * INT64_C(1000000000));
  }
  static Duration FromHours(int64_t v) noexcept {
    return Scaled(v, 3600 * INT64_C(1000000000));
  }
  static Duration Max() noexcept { return Duration(IntLimits<int64_t>::Max()); }
  static Duration Min() noexcept { return Duration(IntLimits<int64_t>::Min()); }

  // Whole units, truncated toward zero.
  constexpr int64_t nanoseconds() const noexcept { return m_ns; }
  constexpr int64_t microseconds() const noexcept { return m_ns / 1000; }
  constexpr int64_t milliseconds() const noexcept { return m_ns / 1000000; }
  constexpr int64_t seconds() const noexcept { return m_ns / 1000000000; }
  constexpr double SecondsAsDouble() const noexcept {
    return static_cast<double>(m_ns) / 1e9;
  }

  constexpr bool IsZero() const noexcept { return m_ns == 0; }
  constexpr bool IsNegative() const noexcept { return m_ns < 0; }

  // Saturating arithmetic.
  Duration operator+(Duration o) const noexcept {
    return Duration(SaturatingAdd<int64_t>(m_ns, o.m_ns));
  }
  Duration operator-(Duration o) const noexcept {
    return Duration(SaturatingSub<int64_t>(m_ns, o.m_ns));
  }
  Duration operator*(int64_t k) const noexcept {
    return Duration(SaturatingMul<int64_t>(m_ns, k));
  }
  // Division by zero aborts like integer division; use a non-zero divisor.
  Duration operator/(int64_t k) const noexcept { return Duration(m_ns / k); }
  Duration &operator+=(Duration o) noexcept { return *this = *this + o; }
  Duration &operator-=(Duration o) noexcept { return *this = *this - o; }

  constexpr bool operator==(Duration o) const noexcept {
    return m_ns == o.m_ns;
  }
  constexpr bool operator!=(Duration o) const noexcept {
    return m_ns != o.m_ns;
  }
  constexpr bool operator<(Duration o) const noexcept { return m_ns < o.m_ns; }
  constexpr bool operator<=(Duration o) const noexcept {
    return m_ns <= o.m_ns;
  }
  constexpr bool operator>(Duration o) const noexcept { return m_ns > o.m_ns; }
  constexpr bool operator>=(Duration o) const noexcept {
    return m_ns >= o.m_ns;
  }
};

// A moment on some clock, as a Duration since that clock's origin. Points
// from different clocks must not be mixed (they are not comparable).
class TimePoint {
  Duration m_since_origin;

public:
  constexpr TimePoint() noexcept {}
  constexpr explicit TimePoint(Duration since_origin) noexcept
      : m_since_origin(since_origin) {}

  constexpr Duration SinceOrigin() const noexcept { return m_since_origin; }

  TimePoint operator+(Duration d) const noexcept {
    return TimePoint(m_since_origin + d);
  }
  TimePoint operator-(Duration d) const noexcept {
    return TimePoint(m_since_origin - d);
  }
  Duration operator-(TimePoint o) const noexcept {
    return m_since_origin - o.m_since_origin;
  }
  constexpr bool operator==(TimePoint o) const noexcept {
    return m_since_origin == o.m_since_origin;
  }
  constexpr bool operator!=(TimePoint o) const noexcept {
    return m_since_origin != o.m_since_origin;
  }
  constexpr bool operator<(TimePoint o) const noexcept {
    return m_since_origin < o.m_since_origin;
  }
  constexpr bool operator<=(TimePoint o) const noexcept {
    return m_since_origin <= o.m_since_origin;
  }
  constexpr bool operator>(TimePoint o) const noexcept {
    return m_since_origin > o.m_since_origin;
  }
  constexpr bool operator>=(TimePoint o) const noexcept {
    return m_since_origin >= o.m_since_origin;
  }
};

namespace detail {
#if defined(_WIN32)
inline int64_t PerformanceCounterNs() noexcept {
  LARGE_INTEGER freq, now;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&now);
  int64_t whole = now.QuadPart / freq.QuadPart;
  int64_t rest = now.QuadPart % freq.QuadPart;
  return whole * INT64_C(1000000000) +
         rest * INT64_C(1000000000) / freq.QuadPart;
}
inline int64_t UnixEpochNs() noexcept {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
               ft.dwLowDateTime; // 100 ns ticks since 1601
  return (static_cast<int64_t>(t) - INT64_C(116444736000000000)) * 100;
}
#else
inline int64_t ClockNs(clockid_t id) noexcept {
  struct timespec ts;
  if (clock_gettime(id, &ts) != 0)
    return 0;
  return static_cast<int64_t>(ts.tv_sec) * INT64_C(1000000000) +
         static_cast<int64_t>(ts.tv_nsec);
}
#endif
} // namespace detail

// Monotonic clock: never goes backwards; origin is unspecified (often boot).
struct SteadyClock {
  static TimePoint Now() noexcept {
#if defined(_WIN32)
    return TimePoint(Duration(detail::PerformanceCounterNs()));
#else
    return TimePoint(Duration(detail::ClockNs(CLOCK_MONOTONIC)));
#endif
  }
};

// Wall clock: time since 1970-01-01 00:00:00 UTC; may jump.
struct SystemClock {
  static TimePoint Now() noexcept {
#if defined(_WIN32)
    return TimePoint(Duration(detail::UnixEpochNs()));
#else
    return TimePoint(Duration(detail::ClockNs(CLOCK_REALTIME)));
#endif
  }
};

// Measures elapsed time on the SteadyClock.
class Stopwatch {
  TimePoint m_start;

public:
  Stopwatch() noexcept : m_start(SteadyClock::Now()) {}
  Duration Elapsed() const noexcept { return SteadyClock::Now() - m_start; }
  // Returns the time since the last start and starts counting again.
  Duration Restart() noexcept {
    TimePoint now = SteadyClock::Now();
    Duration d = now - m_start;
    m_start = now;
    return d;
  }
};

#if !defined(_WIN32)
// Blocks the calling thread for at least `d` (returns early only if the
// sleep is interrupted by a signal and cannot be resumed). Non-positive
// durations return immediately.
inline void SleepFor(Duration d) noexcept {
  if (d.nanoseconds() <= 0)
    return;
  struct timespec req;
  req.tv_sec = static_cast<time_t>(d.seconds());
  req.tv_nsec = static_cast<long>(d.nanoseconds() % INT64_C(1000000000));
  struct timespec rem;
  while (nanosleep(&req, &rem) != 0 && errno == EINTR)
    req = rem;
}
#else
inline void SleepFor(Duration d) noexcept {
  if (d.nanoseconds() > 0)
    Sleep(static_cast<DWORD>(d.milliseconds() + 1));
}
#endif

} // namespace vapor
