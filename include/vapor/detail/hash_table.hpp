#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// The engine shared by HashMap and HashSet (see hash_map.hpp / hash_set.hpp for
// the user-facing documentation).
//
// How it works: open addressing with linear probing. There is one array of
// "control bytes" (empty / deleted / full) and one array of slots. A key's hash
// picks a starting slot; lookups walk forward until they find the key or an
// empty slot. Erasing marks a slot "deleted" (a tombstone) so that probe
// chains stay unbroken; tombstones are dropped when the table is rebuilt or
// when the slot after them is empty. The capacity is always a power of two so
// that `hash & (capacity - 1)` replaces a modulo. The table is rebuilt when
// occupied slots (full + deleted) exceed 7/8, and grows when live elements
// exceed 5/8 of the capacity, so there is always an empty slot and probe
// chains stay short.
//
// `Stored` is what lives in a slot and is moved around while rebuilding.
// `Exposed` is what iterators hand out; it has the same size and layout as
// Stored but with const keys (HashMap stores Pair<K, V> and exposes
// Pair<const K, V>), so users can modify values but not keys.

namespace vapor {
namespace detail {

// True when T declares `is_transparent`, i.e. its call operator accepts keys
// of other types too (see Hash<void> and EqualTo<void>).
template <typename T> struct MakeVoidType {
  typedef void type;
};
template <typename T, typename = void> struct IsTransparent : FalseType {};
template <typename T>
struct IsTransparent<T, typename MakeVoidType<typename T::is_transparent>::type>
    : TrueType {};

template <typename Stored, typename Exposed, typename KeyOf, typename Hasher,
          typename KeyEqual>
class HashTable {
  static_assert(sizeof(Stored) == sizeof(Exposed) &&
                    alignof(Stored) == alignof(Exposed),
                "Exposed must have the same layout as Stored");

public:
  typedef typename KeyOf::key_type key_type;
  static const size_t npos = static_cast<size_t>(-1);

private:
  enum : uint8_t { kEmpty = 0, kDeleted = 1, kFull = 2 };

  Allocator *m_alloc;
  uint8_t *m_ctrl;
  unsigned char *m_slots;
  size_t m_cap;  // number of slots, 0 or a power of two >= 8
  size_t m_size; // full slots
  size_t m_used; // full + deleted slots
  Hasher m_hash;
  KeyEqual m_eq;

  Stored *S(size_t i) const noexcept {
    return reinterpret_cast<Stored *>(m_slots) + i;
  }

  void FreeArrays() noexcept {
    if (m_ctrl)
      m_alloc->Deallocate(m_ctrl, m_cap);
    if (m_slots)
      m_alloc->Deallocate(m_slots, m_cap * sizeof(Stored));
    m_ctrl = nullptr;
    m_slots = nullptr;
  }

  static size_t CapacityFor(size_t live) noexcept {
    size_t cap = 8;
    // live <= cap * 5 / 8, written without overflow
    while (live > cap - cap / 8 * 3 && cap < (VAPOR_SIZE_MAX >> 1))
      cap <<= 1;
    return cap;
  }

  Expected<void, CommonError> Rebuild(size_t new_cap) noexcept {
    if (new_cap > VAPOR_SIZE_MAX / sizeof(Stored))
      return MakeUnexpected(CommonError::OutOfMemory);
    uint8_t *ctrl = static_cast<uint8_t *>(m_alloc->Allocate(new_cap, 1));
    if (!ctrl)
      return MakeUnexpected(CommonError::OutOfMemory);
    unsigned char *slots = static_cast<unsigned char *>(
        m_alloc->Allocate(new_cap * sizeof(Stored), alignof(Stored)));
    if (!slots) {
      m_alloc->Deallocate(ctrl, new_cap);
      return MakeUnexpected(CommonError::OutOfMemory);
    }
    ::memset(ctrl, kEmpty, new_cap);

    uint8_t *old_ctrl = m_ctrl;
    unsigned char *old_slots = m_slots;
    size_t old_cap = m_cap;
    Stored *old = reinterpret_cast<Stored *>(old_slots);

    m_ctrl = ctrl;
    m_slots = slots;
    m_cap = new_cap;
    const size_t mask = new_cap - 1;
    for (size_t i = 0; i < old_cap; ++i) {
      if (old_ctrl[i] != kFull)
        continue;
      size_t j = m_hash(KeyOf()(old[i])) & mask;
      while (m_ctrl[j] != kEmpty)
        j = (j + 1) & mask;
      m_ctrl[j] = kFull;
      ConstructAt<Stored>(S(j), Move(old[i]));
      old[i].~Stored();
    }
    m_used = m_size;
    if (old_ctrl)
      m_alloc->Deallocate(old_ctrl, old_cap);
    if (old_slots)
      m_alloc->Deallocate(old_slots, old_cap * sizeof(Stored));
    return {};
  }

  // Makes sure one more element fits without leaving the table without empty
  // slots.
  Expected<void, CommonError> EnsureRoom() noexcept {
    if (m_cap != 0 && (m_used + 1) <= m_cap - m_cap / 8)
      return {};
    size_t target = CapacityFor(m_size + 1);
    if (target < m_cap)
      target = m_cap; // only tombstones were the problem: rebuild in place
    return Rebuild(target);
  }

  template <typename Key>
  size_t FindIndexHashed(const Key &key, size_t h) const noexcept {
    if (m_cap == 0)
      return npos;
    const size_t mask = m_cap - 1;
    size_t i = h & mask;
    for (;;) {
      uint8_t c = m_ctrl[i];
      if (c == kEmpty)
        return npos;
      if (c == kFull && m_eq(KeyOf()(*S(i)), key))
        return i;
      i = (i + 1) & mask;
    }
  }

  size_t FirstFull(size_t from) const noexcept {
    while (from < m_cap && m_ctrl[from] != kFull)
      ++from;
    return from;
  }

public:
  template <typename V> class Iter {
    friend class HashTable;
    template <typename W> friend class Iter;
    const uint8_t *m_ctrl_p;
    unsigned char *m_slots_p;
    size_t m_idx;
    size_t m_cap_n;

    Iter(const uint8_t *c, unsigned char *s, size_t i, size_t cap) noexcept
        : m_ctrl_p(c), m_slots_p(s), m_idx(i), m_cap_n(cap) {}

  public:
    typedef V value_type;
    typedef ptrdiff_t difference_type;
    typedef V *pointer;
    typedef V &reference;

    Iter() noexcept
        : m_ctrl_p(nullptr), m_slots_p(nullptr), m_idx(0), m_cap_n(0) {}
    // iterator converts to const_iterator
    template <typename W, typename = EnableIfT<IsSame<const W, V>::value &&
                                               !IsSame<W, V>::value>>
    Iter(const Iter<W> &o) noexcept
        : m_ctrl_p(o.m_ctrl_p), m_slots_p(o.m_slots_p), m_idx(o.m_idx),
          m_cap_n(o.m_cap_n) {}

    V &operator*() const noexcept {
      return reinterpret_cast<V *>(m_slots_p)[m_idx];
    }
    V *operator->() const noexcept {
      return reinterpret_cast<V *>(m_slots_p) + m_idx;
    }
    Iter &operator++() noexcept {
      do {
        ++m_idx;
      } while (m_idx < m_cap_n && m_ctrl_p[m_idx] != kFull);
      return *this;
    }
    Iter operator++(int) noexcept {
      Iter t(*this);
      ++*this;
      return t;
    }
    friend bool operator==(const Iter &a, const Iter &b) noexcept {
      return a.m_idx == b.m_idx;
    }
    friend bool operator!=(const Iter &a, const Iter &b) noexcept {
      return a.m_idx != b.m_idx;
    }
  };

  typedef Iter<Exposed> iterator;
  typedef Iter<const Exposed> const_iterator;

  explicit HashTable(Allocator &alloc, const Hasher &h = Hasher(),
                     const KeyEqual &e = KeyEqual())
      : m_alloc(&alloc), m_ctrl(nullptr), m_slots(nullptr), m_cap(0), m_size(0),
        m_used(0), m_hash(h), m_eq(e) {}

  HashTable(HashTable &&o) noexcept
      : m_alloc(o.m_alloc), m_ctrl(o.m_ctrl), m_slots(o.m_slots),
        m_cap(o.m_cap), m_size(o.m_size), m_used(o.m_used),
        m_hash(Move(o.m_hash)), m_eq(Move(o.m_eq)) {
    o.m_ctrl = nullptr;
    o.m_slots = nullptr;
    o.m_cap = o.m_size = o.m_used = 0;
  }

  HashTable &operator=(HashTable &&o) noexcept {
    if (this != &o) {
      clear();
      FreeArrays();
      m_alloc = o.m_alloc;
      m_ctrl = o.m_ctrl;
      m_slots = o.m_slots;
      m_cap = o.m_cap;
      m_size = o.m_size;
      m_used = o.m_used;
      m_hash = Move(o.m_hash);
      m_eq = Move(o.m_eq);
      o.m_ctrl = nullptr;
      o.m_slots = nullptr;
      o.m_cap = o.m_size = o.m_used = 0;
    }
    return *this;
  }

  HashTable(const HashTable &) = delete;
  HashTable &operator=(const HashTable &) = delete;

  ~HashTable() {
    clear();
    FreeArrays();
  }

  Allocator &get_allocator() const noexcept { return *m_alloc; }
  size_t size() const noexcept { return m_size; }
  bool empty() const noexcept { return m_size == 0; }
  size_t bucket_count() const noexcept { return m_cap; }
  float load_factor() const noexcept {
    return m_cap ? static_cast<float>(m_size) / static_cast<float>(m_cap)
                 : 0.0f;
  }
  const Hasher &hash_function() const noexcept { return m_hash; }
  const KeyEqual &key_eq() const noexcept { return m_eq; }

  iterator begin() noexcept {
    return iterator(m_ctrl, m_slots, FirstFull(0), m_cap);
  }
  iterator end() noexcept { return iterator(m_ctrl, m_slots, m_cap, m_cap); }
  const_iterator begin() const noexcept {
    return const_iterator(m_ctrl, m_slots, FirstFull(0), m_cap);
  }
  const_iterator end() const noexcept {
    return const_iterator(m_ctrl, m_slots, m_cap, m_cap);
  }

  iterator MakeIterator(size_t idx) noexcept {
    return iterator(m_ctrl, m_slots, idx == npos ? m_cap : idx, m_cap);
  }
  const_iterator MakeIterator(size_t idx) const noexcept {
    return const_iterator(m_ctrl, m_slots, idx == npos ? m_cap : idx, m_cap);
  }

  size_t IndexOf(const_iterator it) const noexcept { return it.m_idx; }

  Exposed &At(size_t idx) const noexcept {
    return *reinterpret_cast<Exposed *>(S(idx));
  }

  size_t FindIndex(const key_type &key) const noexcept {
    return FindIndexHashed(key, m_hash(key));
  }
  // Lookup by a different key type; the hasher and comparator must be
  // transparent so that equal keys of both types hash and compare alike.
  template <typename Q> size_t FindIndexAs(const Q &key) const noexcept {
    return FindIndexHashed(key, m_hash(key));
  }

  // Looks the key up; if it is absent, makes room and calls make(void *slot)
  // to construct the element in place. Returns {slot index, inserted}. On
  // OutOfMemory the table is unchanged and `make` was not called.
  template <typename Maker>
  Expected<Pair<size_t, bool>, CommonError> InsertWith(const key_type &key,
                                                       Maker make) {
    const size_t h = m_hash(key);
    size_t found = FindIndexHashed(key, h);
    if (found != npos)
      return Pair<size_t, bool>(found, false);
    Expected<void, CommonError> room = EnsureRoom();
    if (!room)
      return MakeUnexpected(room.error());
    const size_t mask = m_cap - 1;
    size_t i = h & mask;
    while (m_ctrl[i] == kFull)
      i = (i + 1) & mask;
    make(static_cast<void *>(S(i)));
    if (m_ctrl[i] == kEmpty)
      ++m_used;
    m_ctrl[i] = kFull;
    ++m_size;
    return Pair<size_t, bool>(i, true);
  }

  void EraseIndex(size_t i) noexcept {
    S(i)->~Stored();
    --m_size;
    const size_t mask = m_cap - 1;
    if (m_ctrl[(i + 1) & mask] == kEmpty) {
      // Nothing probes past this slot, so it and any tombstones right before
      // it can become plain empty slots again.
      m_ctrl[i] = kEmpty;
      --m_used;
      size_t p = (i - 1) & mask;
      while (m_ctrl[p] == kDeleted) {
        m_ctrl[p] = kEmpty;
        --m_used;
        p = (p - 1) & mask;
      }
    } else {
      m_ctrl[i] = kDeleted;
    }
  }

  void clear() noexcept {
    for (size_t i = 0; i < m_cap; ++i)
      if (m_ctrl[i] == kFull)
        S(i)->~Stored();
    if (m_ctrl)
      ::memset(m_ctrl, kEmpty, m_cap);
    m_size = 0;
    m_used = 0;
  }

  // Makes room for `n` elements in total without further allocation.
  Expected<void, CommonError> reserve(size_t n) noexcept {
    if (n <= m_size)
      return {};
    size_t target = CapacityFor(n);
    if (target <= m_cap)
      return {};
    return Rebuild(target);
  }

  void swap(HashTable &o) noexcept {
    HashTable tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }

  // Calls fn(const Stored &) for every live element.
  template <typename Fn> void ForEachStored(Fn fn) const {
    for (size_t i = 0; i < m_cap; ++i)
      if (m_ctrl[i] == kFull)
        fn(static_cast<const Stored &>(*S(i)));
  }
};

template <typename Stored, typename Exposed, typename KeyOf, typename Hasher,
          typename KeyEqual>
const size_t HashTable<Stored, Exposed, KeyOf, Hasher, KeyEqual>::npos;

} // namespace detail
} // namespace vapor
