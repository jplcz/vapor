#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vapor/bit.hpp>
#include <vapor/config.hpp>
#include <vapor/string.hpp>
#include <vapor/string_view.hpp>
#include <vapor/type_traits.hpp>

// Hash<T>: turns a value into a `size_t` for hash tables.
//
// Why: a hash table needs a fast function that spreads keys evenly over its
// buckets; if similar keys (1, 2, 3, ...) land in neighbouring buckets, lookups
// degrade into linear scans. These hashes mix the bits well enough to be
// masked with `hash & (capacity - 1)`.
//
// They are NOT cryptographic: never use them to authenticate data or when an
// attacker controls the keys and could force collisions on purpose.
//
// How: `Hash<int>()(5)`. Provided for integers, enums, bool, floating-point
// numbers, pointers, nullptr, StringView and String. To use your own key type,
// specialise it:
//
//   template <> struct vapor::Hash<MyKey> {
//     size_t operator()(const MyKey &k) const noexcept {
//       return HashCombine(Hash<int>()(k.a), Hash<int>()(k.b));
//     }
//   };
//
// Values that compare equal always hash equal (+0.0 and -0.0 included).
// The hash value of a given key may differ between runs and versions: do not
// store it or depend on its exact value.

namespace vapor {

// Declared with an extra parameter so that specialisations can be constrained.
template <typename T, typename = void> struct Hash;

namespace detail {
// Finaliser from SplitMix64: every input bit affects every output bit.
VAPOR_NODISCARD inline uint64_t Mix64(uint64_t x) noexcept {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}
} // namespace detail

// Hash of a raw byte range (FNV-1a followed by a mixing step).
VAPOR_NODISCARD inline size_t HashBytes(const void *data,
                                        size_t size) noexcept {
  const unsigned char *p = static_cast<const unsigned char *>(data);
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return static_cast<size_t>(detail::Mix64(h));
}

// Combines an existing hash with another one; order matters.
VAPOR_NODISCARD inline size_t HashCombine(size_t seed, size_t h) noexcept {
  return seed ^ (h + static_cast<size_t>(0x9e3779b97f4a7c15ULL) + (seed << 6) +
                 (seed >> 2));
}

template <typename T> struct Hash<T, EnableIfT<IsIntegral<T>::value>> {
  VAPOR_NODISCARD size_t operator()(T v) const noexcept {
    return static_cast<size_t>(detail::Mix64(static_cast<uint64_t>(v)));
  }
};

template <typename T> struct Hash<T, EnableIfT<IsEnum<T>::value>> {
  VAPOR_NODISCARD size_t operator()(T v) const noexcept {
    return static_cast<size_t>(
        detail::Mix64(static_cast<uint64_t>(static_cast<MakeUnsignedT<T>>(v))));
  }
};

template <typename T> struct Hash<T, EnableIfT<IsFloatingPoint<T>::value>> {
  VAPOR_NODISCARD size_t operator()(T v) const noexcept {
    double d = static_cast<double>(v);
    if (d == 0.0)
      return 0; // +0.0 == -0.0 must hash alike
    return static_cast<size_t>(detail::Mix64(BitCast<uint64_t>(d)));
  }
};

template <typename T> struct Hash<T *, void> {
  VAPOR_NODISCARD size_t operator()(T *p) const noexcept {
    return static_cast<size_t>(
        detail::Mix64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p))));
  }
};

template <> struct Hash<decltype(nullptr), void> {
  VAPOR_NODISCARD size_t operator()(decltype(nullptr)) const noexcept {
    return 0;
  }
};

template <> struct Hash<StringView, void> {
  VAPOR_NODISCARD size_t operator()(const StringView &s) const noexcept {
    return HashBytes(s.data(), s.size());
  }
};

// Equal to Hash<StringView> for the same characters, so a String and a
// StringView with the same text can be looked up interchangeably.
template <> struct Hash<String, void> {
  VAPOR_NODISCARD size_t operator()(const String &s) const noexcept {
    return HashBytes(s.data(), s.size());
  }
};

} // namespace vapor
