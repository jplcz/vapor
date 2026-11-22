#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/bit.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/vector.hpp>

// DynamicBitset: a resizable array of on/off flags packed into 64-bit words.
//
// Why: Bitset<N> needs N at compile time. When the number of flags is only
// known at run time (which of the loaded entities are alive, a visited-set for
// a graph walk, a free-list of slots) a DynamicBitset gives the same dense
// storage (1 bit per flag instead of 1 byte) and the same whole-set
// operations.
//
// How:
//
//   DynamicBitset b;
//   b.resize(100);                 // Expected<void, ...>; new bits are 0
//   b.set(3); b.test(3); b.flip(3);
//   b.count(); b.any(); b.none(); b.all();
//   for (size_t i = b.FindFirst(); i != b.size(); i = b.FindNext(i)) ...
//   b |= other;                    // both operands must have the same size
//
// Differences from Bitset: sizes change at run time, so growing returns an
// Expected (OutOfMemory). Like Bitset, test/set/reset/flip abort on a position
// >= size(); use checked_test for an Expected. Combining two sets of different
// sizes aborts. Copying is the explicit, fallible Clone(). Bits past size()
// are kept at zero internally so count() and comparison are always exact.

namespace vapor {

class DynamicBitset {
  static size_t WordsFor(size_t bits) noexcept { return (bits + 63) / 64; }

  Vector<uint64_t> m_words;
  size_t m_size;

  void TrimTail() noexcept {
    if (m_size % 64 != 0)
      m_words[m_words.size() - 1] &=
          (static_cast<uint64_t>(1) << (m_size % 64)) - 1;
  }
  void CheckPos(size_t pos) const {
    if (pos >= m_size)
      abort();
  }
  void CheckSame(const DynamicBitset &o) const {
    if (m_size != o.m_size)
      abort();
  }

public:
  explicit DynamicBitset(
      Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_words(alloc), m_size(0) {}
  DynamicBitset(DynamicBitset &&o) noexcept
      : m_words(Move(o.m_words)), m_size(o.m_size) {
    o.m_size = 0;
  }
  DynamicBitset &operator=(DynamicBitset &&o) noexcept {
    if (this != &o) {
      m_words = Move(o.m_words);
      m_size = o.m_size;
      o.m_size = 0;
    }
    return *this;
  }
  DynamicBitset(const DynamicBitset &) = delete;
  DynamicBitset &operator=(const DynamicBitset &) = delete;

  VAPOR_NODISCARD Expected<DynamicBitset, CommonError> Clone() const {
    Expected<Vector<uint64_t>, CommonError> w = m_words.Clone();
    if (!w)
      return MakeUnexpected(w.error());
    DynamicBitset out(m_words.get_allocator());
    out.m_words = Move(w.value());
    out.m_size = m_size;
    return out;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept {
    return m_words.get_allocator();
  }
  VAPOR_NODISCARD size_t size() const noexcept { return m_size; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }

  // Changes the number of bits; added bits get `value`.
  Expected<void, CommonError> resize(size_t bits, bool value = false) {
    if (bits > VAPOR_SIZE_MAX - 63)
      return MakeUnexpected(CommonError::OutOfMemory);
    size_t old_size = m_size;
    Expected<void, CommonError> r = m_words.resize(WordsFor(bits));
    if (!r)
      return r;
    // Added whole words start as zero; fill them when asked.
    if (value && bits > old_size) {
      size_t i = old_size;
      for (; i < bits && i % 64 != 0; ++i)
        m_words[i / 64] |= static_cast<uint64_t>(1) << (i % 64);
      if (i % 64 == 0)
        for (size_t w = i / 64; w < m_words.size(); ++w)
          m_words[w] = ~static_cast<uint64_t>(0);
    }
    m_size = bits;
    TrimTail();
    return {};
  }
  void clear() noexcept {
    m_words.clear();
    m_size = 0;
  }

  Expected<void, CommonError> push_back(bool value) {
    return resize(m_size + 1, value);
  }

  VAPOR_NODISCARD bool test(size_t pos) const {
    CheckPos(pos);
    return (m_words[pos / 64] >> (pos % 64)) & 1u;
  }
  VAPOR_NODISCARD Expected<bool, CommonError>
  checked_test(size_t pos) const noexcept {
    if (pos >= m_size)
      return MakeUnexpected(CommonError::OutOfBounds);
    return (m_words[pos / 64] >> (pos % 64)) & 1u;
  }
  VAPOR_NODISCARD bool operator[](size_t pos) const { return test(pos); }

  DynamicBitset &set(size_t pos, bool value = true) {
    CheckPos(pos);
    uint64_t mask = static_cast<uint64_t>(1) << (pos % 64);
    if (value)
      m_words[pos / 64] |= mask;
    else
      m_words[pos / 64] &= ~mask;
    return *this;
  }
  DynamicBitset &reset(size_t pos) { return set(pos, false); }
  DynamicBitset &flip(size_t pos) {
    CheckPos(pos);
    m_words[pos / 64] ^= static_cast<uint64_t>(1) << (pos % 64);
    return *this;
  }
  DynamicBitset &set() noexcept {
    for (size_t i = 0; i < m_words.size(); ++i)
      m_words[i] = ~static_cast<uint64_t>(0);
    if (m_size != 0)
      TrimTail();
    return *this;
  }
  DynamicBitset &reset() noexcept {
    for (size_t i = 0; i < m_words.size(); ++i)
      m_words[i] = 0;
    return *this;
  }
  DynamicBitset &flip() noexcept {
    for (size_t i = 0; i < m_words.size(); ++i)
      m_words[i] = ~m_words[i];
    if (m_size != 0)
      TrimTail();
    return *this;
  }

  VAPOR_NODISCARD size_t count() const noexcept {
    size_t n = 0;
    for (size_t i = 0; i < m_words.size(); ++i)
      n += static_cast<size_t>(PopCount(m_words[i]));
    return n;
  }
  VAPOR_NODISCARD bool any() const noexcept {
    for (size_t i = 0; i < m_words.size(); ++i)
      if (m_words[i] != 0)
        return true;
    return false;
  }
  VAPOR_NODISCARD bool none() const noexcept { return !any(); }
  VAPOR_NODISCARD bool all() const noexcept { return count() == m_size; }

  // Position of the first set bit, or size() when there is none.
  VAPOR_NODISCARD size_t FindFirst() const noexcept { return FindFrom(0); }
  // Position of the first set bit after `pos`, or size() when there is none.
  VAPOR_NODISCARD size_t FindNext(size_t pos) const noexcept {
    return pos >= m_size ? m_size : FindFrom(pos + 1);
  }

  DynamicBitset &operator&=(const DynamicBitset &o) {
    CheckSame(o);
    for (size_t i = 0; i < m_words.size(); ++i)
      m_words[i] &= o.m_words[i];
    return *this;
  }
  DynamicBitset &operator|=(const DynamicBitset &o) {
    CheckSame(o);
    for (size_t i = 0; i < m_words.size(); ++i)
      m_words[i] |= o.m_words[i];
    return *this;
  }
  DynamicBitset &operator^=(const DynamicBitset &o) {
    CheckSame(o);
    for (size_t i = 0; i < m_words.size(); ++i)
      m_words[i] ^= o.m_words[i];
    return *this;
  }

  void swap(DynamicBitset &o) noexcept {
    DynamicBitset tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }

  VAPOR_NODISCARD friend bool operator==(const DynamicBitset &a,
                                         const DynamicBitset &b) noexcept {
    if (a.m_size != b.m_size)
      return false;
    for (size_t i = 0; i < a.m_words.size(); ++i)
      if (a.m_words[i] != b.m_words[i])
        return false;
    return true;
  }
  VAPOR_NODISCARD friend bool operator!=(const DynamicBitset &a,
                                         const DynamicBitset &b) noexcept {
    return !(a == b);
  }

private:
  size_t FindFrom(size_t pos) const noexcept {
    if (pos >= m_size)
      return m_size;
    size_t w = pos / 64;
    uint64_t cur = m_words[w] & (~static_cast<uint64_t>(0) << (pos % 64));
    for (;;) {
      if (cur != 0)
        return w * 64 + static_cast<size_t>(CountTrailingZeros(cur));
      if (++w >= m_words.size())
        return m_size;
      cur = m_words[w];
    }
  }
};

} // namespace vapor
