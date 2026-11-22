#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vapor/bit.hpp>
#include <vapor/common.hpp>
#include <vapor/config.hpp>
#include <vapor/expected.hpp>

// Bitset<N>: N on/off flags packed into 64-bit words, API-compatible with
// std::bitset.
//
// Why: a bool array wastes 8x the memory and cannot be combined with one
// instruction. A Bitset keeps flags dense (e.g. "which slots are in use") and
// supports whole-set operations (`&`, `|`, `count()`), and it never allocates.
//
// How: `Bitset<100> b; b.set(3); b.test(3); b.count(); b |= other;`.
// `FindFirst()` / `FindNext(pos)` walk the set bits quickly; they return `N`
// when there is none. Deviations from std::bitset (which throws): test(),
// set(), reset(), flip() abort on a position >= N; `checked_test(pos)` returns
// an `Expected` instead. There is no string conversion.

namespace vapor {

template <size_t N> class Bitset {
  static constexpr size_t kWordBits = 64;
  static constexpr size_t kWords = (N + kWordBits - 1) / kWordBits;
  // Mask of the valid bits in the last word.
  static constexpr uint64_t kTailMask =
      (N % kWordBits == 0) ? ~uint64_t(0)
                           : ((uint64_t(1) << (N % kWordBits)) - 1);

  uint64_t m_words[kWords];

  void TrimTail() noexcept { m_words[kWords - 1] &= kTailMask; }
  static void CheckPos(size_t pos) {
    if (pos >= N)
      abort();
  }

public:
  // Proxy returned by non-const operator[] so that `b[3] = true` works.
  class Reference {
    Bitset *m_set;
    size_t m_pos;
    friend class Bitset;
    Reference(Bitset &set, size_t pos) noexcept : m_set(&set), m_pos(pos) {}

  public:
    Reference &operator=(bool v) noexcept {
      m_set->set(m_pos, v);
      return *this;
    }
    Reference &operator=(const Reference &o) noexcept {
      return *this = static_cast<bool>(o);
    }
    operator bool() const noexcept { return m_set->test(m_pos); }
    bool operator~() const noexcept { return !m_set->test(m_pos); }
    Reference &flip() noexcept {
      m_set->flip(m_pos);
      return *this;
    }
  };

  constexpr Bitset() noexcept : m_words() {}
  // Bits of `value` become bits 0..63 (bits beyond N are dropped).
  explicit Bitset(unsigned long long value) noexcept : m_words() {
    m_words[0] = value;
    TrimTail();
  }

  VAPOR_NODISCARD constexpr size_t size() const noexcept { return N; }

  VAPOR_NODISCARD bool test(size_t pos) const {
    CheckPos(pos);
    return (m_words[pos / kWordBits] >> (pos % kWordBits)) & 1;
  }
  VAPOR_NODISCARD Expected<bool, CommonError>
  checked_test(size_t pos) const noexcept {
    if (pos >= N)
      return MakeUnexpected(CommonError::OutOfBounds);
    return test(pos);
  }
  VAPOR_NODISCARD bool operator[](size_t pos) const { return test(pos); }
  VAPOR_NODISCARD Reference operator[](size_t pos) {
    CheckPos(pos);
    return Reference(*this, pos);
  }

  Bitset &set() noexcept {
    for (size_t i = 0; i < kWords; ++i)
      m_words[i] = ~uint64_t(0);
    TrimTail();
    return *this;
  }
  Bitset &set(size_t pos, bool value = true) {
    CheckPos(pos);
    uint64_t bit = uint64_t(1) << (pos % kWordBits);
    if (value)
      m_words[pos / kWordBits] |= bit;
    else
      m_words[pos / kWordBits] &= ~bit;
    return *this;
  }
  Bitset &reset() noexcept {
    for (size_t i = 0; i < kWords; ++i)
      m_words[i] = 0;
    return *this;
  }
  Bitset &reset(size_t pos) { return set(pos, false); }
  Bitset &flip() noexcept {
    for (size_t i = 0; i < kWords; ++i)
      m_words[i] = ~m_words[i];
    TrimTail();
    return *this;
  }
  Bitset &flip(size_t pos) {
    CheckPos(pos);
    m_words[pos / kWordBits] ^= uint64_t(1) << (pos % kWordBits);
    return *this;
  }

  VAPOR_NODISCARD size_t count() const noexcept {
    size_t n = 0;
    for (size_t i = 0; i < kWords; ++i)
      n += static_cast<size_t>(PopCount(m_words[i]));
    return n;
  }
  VAPOR_NODISCARD bool any() const noexcept {
    for (size_t i = 0; i < kWords; ++i)
      if (m_words[i])
        return true;
    return false;
  }
  VAPOR_NODISCARD bool none() const noexcept { return !any(); }
  VAPOR_NODISCARD bool all() const noexcept { return count() == N; }

  // Position of the first set bit, or N when no bit is set.
  VAPOR_NODISCARD size_t FindFirst() const noexcept { return FindFrom(0); }
  // Position of the first set bit after `pos`, or N when there is none.
  VAPOR_NODISCARD size_t FindNext(size_t pos) const noexcept {
    return pos + 1 >= N ? N : FindFrom(pos + 1);
  }

  // The low 64 bits as a number; aborts if any higher bit is set.
  VAPOR_NODISCARD uint64_t ToUint64() const {
    for (size_t i = 1; i < kWords; ++i)
      if (m_words[i])
        abort();
    return m_words[0];
  }

  Bitset &operator&=(const Bitset &o) noexcept {
    for (size_t i = 0; i < kWords; ++i)
      m_words[i] &= o.m_words[i];
    return *this;
  }
  Bitset &operator|=(const Bitset &o) noexcept {
    for (size_t i = 0; i < kWords; ++i)
      m_words[i] |= o.m_words[i];
    return *this;
  }
  Bitset &operator^=(const Bitset &o) noexcept {
    for (size_t i = 0; i < kWords; ++i)
      m_words[i] ^= o.m_words[i];
    return *this;
  }
  // Shifts move bit i to i + shift (left) or i - shift (right); bits shifted
  // out are lost and zeros shift in.
  Bitset &operator<<=(size_t shift) noexcept {
    if (shift >= N)
      return reset();
    size_t w = shift / kWordBits, b = shift % kWordBits;
    for (size_t i = kWords; i-- > 0;) {
      uint64_t v = i >= w ? m_words[i - w] << b : 0;
      if (b && i > w)
        v |= m_words[i - w - 1] >> (kWordBits - b);
      m_words[i] = v;
    }
    TrimTail();
    return *this;
  }
  Bitset &operator>>=(size_t shift) noexcept {
    if (shift >= N)
      return reset();
    size_t w = shift / kWordBits, b = shift % kWordBits;
    for (size_t i = 0; i < kWords; ++i) {
      size_t src = i + w;
      uint64_t v = src < kWords ? m_words[src] >> b : 0;
      if (b && src + 1 < kWords)
        v |= m_words[src + 1] << (kWordBits - b);
      m_words[i] = v;
    }
    return *this;
  }
  VAPOR_NODISCARD Bitset operator<<(size_t shift) const noexcept {
    Bitset r(*this);
    return r <<= shift;
  }
  VAPOR_NODISCARD Bitset operator>>(size_t shift) const noexcept {
    Bitset r(*this);
    return r >>= shift;
  }
  VAPOR_NODISCARD Bitset operator~() const noexcept {
    Bitset r(*this);
    return r.flip();
  }

  void swap(Bitset &o) noexcept {
    for (size_t i = 0; i < kWords; ++i) {
      uint64_t t = m_words[i];
      m_words[i] = o.m_words[i];
      o.m_words[i] = t;
    }
  }

  VAPOR_NODISCARD friend bool operator==(const Bitset &a,
                                         const Bitset &b) noexcept {
    for (size_t i = 0; i < kWords; ++i)
      if (a.m_words[i] != b.m_words[i])
        return false;
    return true;
  }
  VAPOR_NODISCARD friend bool operator!=(const Bitset &a,
                                         const Bitset &b) noexcept {
    return !(a == b);
  }
  VAPOR_NODISCARD friend Bitset operator&(Bitset a, const Bitset &b) noexcept {
    return a &= b;
  }
  VAPOR_NODISCARD friend Bitset operator|(Bitset a, const Bitset &b) noexcept {
    return a |= b;
  }
  VAPOR_NODISCARD friend Bitset operator^(Bitset a, const Bitset &b) noexcept {
    return a ^= b;
  }

private:
  size_t FindFrom(size_t pos) const noexcept {
    size_t w = pos / kWordBits;
    if (w >= kWords)
      return N;
    uint64_t cur = m_words[w] & (~uint64_t(0) << (pos % kWordBits));
    for (;;) {
      if (cur)
        return w * kWordBits + static_cast<size_t>(CountTrailingZeros(cur));
      if (++w >= kWords)
        return N;
      cur = m_words[w];
    }
  }
};

// Zero-length bitset: nothing to store, nothing can be set.
template <> class Bitset<0> {
public:
  constexpr Bitset() noexcept {}
  explicit Bitset(unsigned long long) noexcept {}
  VAPOR_NODISCARD constexpr size_t size() const noexcept { return 0; }
  VAPOR_NODISCARD bool test(size_t) const { abort(); }
  VAPOR_NODISCARD Expected<bool, CommonError>
  checked_test(size_t) const noexcept {
    return MakeUnexpected(CommonError::OutOfBounds);
  }
  Bitset &set() noexcept { return *this; }
  Bitset &reset() noexcept { return *this; }
  Bitset &flip() noexcept { return *this; }
  VAPOR_NODISCARD size_t count() const noexcept { return 0; }
  VAPOR_NODISCARD bool any() const noexcept { return false; }
  VAPOR_NODISCARD bool none() const noexcept { return true; }
  VAPOR_NODISCARD bool all() const noexcept { return true; }
  VAPOR_NODISCARD size_t FindFirst() const noexcept { return 0; }
  VAPOR_NODISCARD size_t FindNext(size_t) const noexcept { return 0; }
  VAPOR_NODISCARD uint64_t ToUint64() const noexcept { return 0; }
  VAPOR_NODISCARD friend bool operator==(const Bitset &, const Bitset &) {
    return true;
  }
  VAPOR_NODISCARD friend bool operator!=(const Bitset &, const Bitset &) {
    return false;
  }
};

template <size_t N> void Swap(Bitset<N> &a, Bitset<N> &b) noexcept {
  a.swap(b);
}

} // namespace vapor
