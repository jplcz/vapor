#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <vapor/bit.hpp>
#include <vapor/common.hpp>
#include <vapor/config.hpp>
#include <vapor/span.hpp>

// BitView: treat an existing byte buffer as an array of bits, without copying.
//
// Why: flags, allocation bitmaps, network headers and hardware registers are
// byte buffers where each bit has a meaning. Hand-written `buf[i / 8] & (1 <<
// (i % 8))` is easy to get wrong; Bitset owns its storage, while a BitView
// borrows memory you already have.
//
// How: `uint8_t buf[4] = {}; BitView v(buf, 4); v.set(9); v.test(9);`
// Bit i lives in byte i / 8 at position i % 8, counted from the least
// significant bit, on every platform. The view does not own the memory; the
// buffer must outlive it. `test`/`set`/... check the index with an assertion
// (the library's out-of-bounds policy); `checked_test` returns an Expected.

namespace vapor {

template <typename Byte> class BasicBitView {
  static_assert(sizeof(Byte) == 1, "BitView works on bytes");

public:
  typedef size_t size_type;
  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr BasicBitView() noexcept : data_(nullptr), bits_(0) {}
  // View of all bits of `bytes`.
  constexpr BasicBitView(Span<Byte> bytes) noexcept
      : data_(bytes.data()), bits_(bytes.size() * 8) {}
  // View of the first `bits` bits of `bytes`; clamped to the buffer size.
  BasicBitView(Span<Byte> bytes, size_type bits) noexcept
      : data_(bytes.data()),
        bits_(bits < bytes.size() * 8 ? bits : bytes.size() * 8) {}
  BasicBitView(Byte *bytes, size_type byte_count) noexcept
      : data_(bytes), bits_(byte_count * 8) {}

  // A mutable view converts to a read-only one.
  template <typename B, typename = EnableIfT<IsSame<const B, Byte>::value &&
                                             !IsConst<B>::value>>
  constexpr BasicBitView(const BasicBitView<B> &o) noexcept
      : data_(o.data()), bits_(o.size()) {}

  VAPOR_NODISCARD constexpr size_type size() const noexcept { return bits_; }
  VAPOR_NODISCARD constexpr bool empty() const noexcept { return bits_ == 0; }
  VAPOR_NODISCARD constexpr Byte *data() const noexcept { return data_; }

  VAPOR_NODISCARD bool test(size_type i) const noexcept {
    assert(i < bits_);
    return ((static_cast<unsigned>(data_[i >> 3]) >> (i & 7)) & 1u) != 0;
  }
  VAPOR_NODISCARD bool operator[](size_type i) const noexcept {
    return test(i);
  }
  VAPOR_NODISCARD Expected<bool, CommonError>
  checked_test(size_type i) const noexcept {
    if (i >= bits_)
      return MakeUnexpected(CommonError::OutOfBounds);
    return test(i);
  }

  void set(size_type i, bool value = true) const noexcept {
    assert(i < bits_);
    Byte mask = static_cast<Byte>(1u << (i & 7));
    if (value)
      data_[i >> 3] = static_cast<Byte>(data_[i >> 3] | mask);
    else
      data_[i >> 3] = static_cast<Byte>(data_[i >> 3] & ~mask);
  }
  void reset(size_type i) const noexcept { set(i, false); }
  void flip(size_type i) const noexcept { set(i, !test(i)); }
  void Fill(bool value) const noexcept {
    for (size_type i = 0; i < bits_; ++i)
      set(i, value);
  }

  // Number of bits set to 1.
  VAPOR_NODISCARD size_type count() const noexcept {
    size_type n = 0;
    for (size_type i = 0; i < bits_; ++i)
      if (test(i))
        ++n;
    return n;
  }
  VAPOR_NODISCARD bool any() const noexcept { return FindFirst() != npos; }
  VAPOR_NODISCARD bool none() const noexcept { return !any(); }

  // Index of the first set bit at or after `from`, or npos.
  VAPOR_NODISCARD size_type FindFirst(size_type from = 0) const noexcept {
    size_type i = from;
    while (i < bits_) {
      if ((i & 7) == 0 && i + 8 <= bits_ && data_[i >> 3] == 0) {
        i += 8;
        continue;
      }
      if (test(i))
        return i;
      ++i;
    }
    return npos;
  }
  VAPOR_NODISCARD size_type FindNext(size_type after) const noexcept {
    return after + 1 < bits_ ? FindFirst(after + 1) : npos;
  }
  // Index of the first bit at or after `from` that is 0, or npos.
  VAPOR_NODISCARD size_type FindFirstZero(size_type from = 0) const noexcept {
    for (size_type i = from; i < bits_; ++i)
      if (!test(i))
        return i;
    return npos;
  }

private:
  Byte *data_;
  size_type bits_;
};

template <typename Byte> constexpr size_t BasicBitView<Byte>::npos;

typedef BasicBitView<uint8_t> BitView;
typedef BasicBitView<const uint8_t> ConstBitView;

} // namespace vapor
