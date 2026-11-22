#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vapor/bit.hpp>
#include <vapor/checked_math.hpp>
#include <vapor/chrono.hpp>
#include <vapor/hash.hpp>
#include <vapor/span.hpp>
#include <vapor/utility.hpp>

// Random: a fast, seedable pseudo-random number generator (xoshiro256**) plus
// helpers for common distributions.
//
// Why: simulations, games, tests, shuffling and sampling need many random
// numbers quickly and, for tests, the SAME numbers every run. Seeding with a
// fixed value gives a reproducible sequence; `Random::FromClock()` gives a
// different one each run.
//
// How:
//   Random rng(12345);                // reproducible
//   uint32_t dice = rng.Uniform<uint32_t>(1, 6);   // inclusive range, unbiased
//   double x = rng.NextDouble();      // [0, 1)
//   rng.Shuffle(vec.begin(), vec.end());
//
// NOT cryptographically secure: the whole future sequence can be predicted
// from a few outputs. Never use it for keys, tokens, passwords or anything an
// attacker should not guess; use the operating system's secure random source
// for those. A generator is not thread-safe: use one per thread.
//
// It also satisfies the standard "uniform random bit generator" shape
// (result_type, min(), max(), operator()), so it works with algorithms
// written for that.

namespace vapor {

class Random {
  uint64_t m_s[4];

  static uint64_t SplitMix(uint64_t &state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    return detail::Mix64(state);
  }

public:
  typedef uint64_t result_type;
  static constexpr result_type min() noexcept { return 0; }
  static constexpr result_type max() noexcept {
    return ~static_cast<uint64_t>(0);
  }

  explicit Random(uint64_t seed = 0x853c49e6748fea9bULL) noexcept {
    Seed(seed);
  }

  // Seeds from the clocks and a stack address: different on each run, but
  // guessable. For reproducible runs pass an explicit seed instead.
  static Random FromClock() noexcept {
    uint64_t a =
        static_cast<uint64_t>(SystemClock::Now().SinceOrigin().nanoseconds());
    uint64_t b =
        static_cast<uint64_t>(SteadyClock::Now().SinceOrigin().nanoseconds());
    int local = 0;
    uint64_t c = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&local));
    return Random(a ^ detail::Mix64(b) ^ (c << 16));
  }

  void Seed(uint64_t seed) noexcept {
    uint64_t st = seed;
    for (int i = 0; i < 4; ++i)
      m_s[i] = SplitMix(st);
  }

  uint64_t Next64() noexcept {
    const uint64_t result = RotateLeft(m_s[1] * 5, 7) * 9;
    const uint64_t t = m_s[1] << 17;
    m_s[2] ^= m_s[0];
    m_s[3] ^= m_s[1];
    m_s[1] ^= m_s[2];
    m_s[0] ^= m_s[3];
    m_s[2] ^= t;
    m_s[3] = RotateLeft(m_s[3], 45);
    return result;
  }
  uint32_t Next32() noexcept { return static_cast<uint32_t>(Next64() >> 32); }
  result_type operator()() noexcept { return Next64(); }

  // Uniform integer in [0, n) without modulo bias; n must be non-zero.
  uint64_t Below(uint64_t n) noexcept {
    // Reject the few values at the top that would make small results more
    // likely than large ones.
    const uint64_t threshold = (0 - n) % n;
    uint64_t r = Next64();
    while (r < threshold)
      r = Next64();
    return r % n;
  }

  // Uniform integer in [lo, hi] (both inclusive); requires lo <= hi.
  template <typename T> T Uniform(T lo, T hi) noexcept {
    static_assert(IsIntegral<T>::value, "Uniform needs an integer type");
    typedef MakeUnsignedT<T> U;
    const U span = static_cast<U>(static_cast<U>(hi) - static_cast<U>(lo));
    if (span == static_cast<U>(~U(0))) // the whole range of the type
      return static_cast<T>(static_cast<U>(Next64()));
    const uint64_t off = Below(static_cast<uint64_t>(span) + 1);
    return static_cast<T>(
        static_cast<U>(static_cast<U>(lo) + static_cast<U>(off)));
  }

  // Double in [0, 1) with 53 random bits.
  double NextDouble() noexcept {
    return static_cast<double>(Next64() >> 11) * (1.0 / 9007199254740992.0);
  }
  // Double in [lo, hi).
  double Uniform(double lo, double hi) noexcept {
    return lo + (hi - lo) * NextDouble();
  }
  // true with probability p (clamped to [0, 1]).
  bool NextBool(double p = 0.5) noexcept { return NextDouble() < p; }

  // Fills a byte buffer with random bytes.
  void Fill(Span<unsigned char> out) noexcept {
    size_t i = 0;
    while (i < out.size()) {
      uint64_t v = Next64();
      for (int b = 0; b < 8 && i < out.size(); ++b, ++i) {
        out[i] = static_cast<unsigned char>(v & 0xFF);
        v >>= 8;
      }
    }
  }

  // Fisher-Yates shuffle of a random-access range.
  template <typename It> void Shuffle(It first, It last) noexcept {
    size_t n = static_cast<size_t>(last - first);
    for (size_t i = n; i > 1; --i) {
      size_t j = static_cast<size_t>(Below(i));
      Swap(first[i - 1], first[j]);
    }
  }
};

} // namespace vapor
