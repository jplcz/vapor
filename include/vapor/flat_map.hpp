#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/vector.hpp>

// FlatMap<K, V>: an ordered dictionary stored as one sorted array, modelled
// on std::map / C++23 std::flat_map.
//
// Why: a node-based tree allocates per element and chases pointers; a sorted
// array is one allocation, cache friendly, iterates in key order and is
// compact. Lookup is O(log n) by binary search. The price: inserting or
// erasing in the middle shifts later elements, O(n). It is the right choice
// for small or read-mostly maps (configuration, lookup tables, sorted
// registries), and when you need ordered iteration or lower_bound/upper_bound.
// For many frequent insertions of large maps prefer HashMap.
//
// How:
//
//   FlatMap<int, String> m;
//   auto r = m.try_emplace(3, ...);   // Expected<Pair<iterator, bool>, ...>
//   auto it = m.find(3);              // end() when absent
//   m.at(3);                          // aborts when absent
//   m.checked_at(3);                  // Expected<ReferenceWrapper<V>, ...>
//   for (auto &kv : m) ...            // ascending key order
//   m.lower_bound(k), m.upper_bound(k), m.equal_range(k)
//
// The third template argument is the key ordering (default Less<K>).
//
// Same rules as the other vapor containers: no exceptions, so anything that
// can allocate returns Expected and leaves the map unchanged on failure;
// there is no operator[] (it would have to insert); copying is the explicit
// fallible Clone(). Inserting or erasing invalidates all iterators and
// references. Keys are immutable through iterators (value_type is
// Pair<const K, V>), values are mutable. Not thread-safe.

namespace vapor {

template <typename K, typename V, typename Cmp = Less<K>> class FlatMap {
public:
  typedef K key_type;
  typedef V mapped_type;
  typedef Pair<const K, V> value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef Cmp key_compare;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef value_type *iterator;
  typedef const value_type *const_iterator;
  typedef Expected<Pair<iterator, bool>, CommonError> InsertResult;

private:
  typedef Pair<K, V> Stored;
  Vector<Stored> m_items;
  Cmp m_cmp;

  // Same layout trick as HashMap: elements live as Pair<K, V> so they can be
  // moved while shifting, and are exposed as Pair<const K, V>.
  static iterator Expose(Stored *p) noexcept {
    return reinterpret_cast<iterator>(p);
  }
  static const_iterator Expose(const Stored *p) noexcept {
    return reinterpret_cast<const_iterator>(p);
  }

  // Index of the first element whose key is not less than `key`.
  size_t LowerIndex(const K &key) const noexcept {
    size_t lo = 0, hi = m_items.size();
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (m_cmp(m_items[mid].first, key))
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }
  size_t UpperIndex(const K &key) const noexcept {
    size_t lo = 0, hi = m_items.size();
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (!m_cmp(key, m_items[mid].first))
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }
  // Index of the element equivalent to `key`, or size() when absent.
  size_t FindIndex(const K &key) const noexcept {
    size_t i = LowerIndex(key);
    if (i < m_items.size() && !m_cmp(key, m_items[i].first))
      return i;
    return m_items.size();
  }

public:
  explicit FlatMap(Allocator &alloc = DefaultAllocator::GetDefault(),
                   const Cmp &cmp = Cmp()) noexcept
      : m_items(alloc), m_cmp(cmp) {}

  FlatMap(FlatMap &&) = default;
  FlatMap &operator=(FlatMap &&) = default;
  FlatMap(const FlatMap &) = delete;
  FlatMap &operator=(const FlatMap &) = delete;

  VAPOR_NODISCARD Expected<FlatMap, CommonError> Clone() const {
    Expected<Vector<Stored>, CommonError> items = m_items.Clone();
    if (!items)
      return MakeUnexpected(items.error());
    FlatMap out(m_items.get_allocator(), m_cmp);
    out.m_items = Move(items.value());
    return out;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept {
    return m_items.get_allocator();
  }
  VAPOR_NODISCARD key_compare key_comp() const { return m_cmp; }

  VAPOR_NODISCARD size_type size() const noexcept { return m_items.size(); }
  VAPOR_NODISCARD bool empty() const noexcept { return m_items.empty(); }
  VAPOR_NODISCARD size_type capacity() const noexcept {
    return m_items.capacity();
  }
  Expected<void, CommonError> reserve(size_type n) noexcept {
    return m_items.reserve(n);
  }

  VAPOR_NODISCARD iterator begin() noexcept { return Expose(m_items.begin()); }
  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return Expose(m_items.begin());
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return begin(); }
  VAPOR_NODISCARD iterator end() noexcept { return Expose(m_items.end()); }
  VAPOR_NODISCARD const_iterator end() const noexcept {
    return Expose(m_items.end());
  }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return end(); }

  VAPOR_NODISCARD iterator find(const K &key) noexcept {
    return begin() + FindIndex(key);
  }
  VAPOR_NODISCARD const_iterator find(const K &key) const noexcept {
    return begin() + FindIndex(key);
  }
  VAPOR_NODISCARD bool contains(const K &key) const noexcept {
    return FindIndex(key) != size();
  }
  VAPOR_NODISCARD size_type count(const K &key) const noexcept {
    return contains(key) ? 1 : 0;
  }
  VAPOR_NODISCARD iterator lower_bound(const K &key) noexcept {
    return begin() + LowerIndex(key);
  }
  VAPOR_NODISCARD const_iterator lower_bound(const K &key) const noexcept {
    return begin() + LowerIndex(key);
  }
  VAPOR_NODISCARD iterator upper_bound(const K &key) noexcept {
    return begin() + UpperIndex(key);
  }
  VAPOR_NODISCARD const_iterator upper_bound(const K &key) const noexcept {
    return begin() + UpperIndex(key);
  }
  VAPOR_NODISCARD Pair<iterator, iterator> equal_range(const K &key) noexcept {
    return Pair<iterator, iterator>(lower_bound(key), upper_bound(key));
  }
  VAPOR_NODISCARD Pair<const_iterator, const_iterator>
  equal_range(const K &key) const noexcept {
    return Pair<const_iterator, const_iterator>(lower_bound(key),
                                                upper_bound(key));
  }

  // Reference to the mapped value; aborts when the key is absent.
  VAPOR_NODISCARD V &at(const K &key) noexcept {
    size_t i = FindIndex(key);
    if (i == size())
      abort();
    return m_items[i].second;
  }
  VAPOR_NODISCARD const V &at(const K &key) const noexcept {
    size_t i = FindIndex(key);
    if (i == size())
      abort();
    return m_items[i].second;
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<V>, CommonError>
  checked_at(const K &key) noexcept {
    size_t i = FindIndex(key);
    if (i == size())
      return MakeUnexpected(CommonError::ValueNotFound);
    return ReferenceWrapper<V>(m_items[i].second);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const V>, CommonError>
  checked_at(const K &key) const noexcept {
    size_t i = FindIndex(key);
    if (i == size())
      return MakeUnexpected(CommonError::ValueNotFound);
    return ReferenceWrapper<const V>(m_items[i].second);
  }

  // --- modification
  // Insertions return {iterator to the element, true if inserted}; for an
  // existing key nothing changes and `false` is returned.

  VAPOR_NODISCARD InsertResult insert(const Stored &kv) {
    size_t i = LowerIndex(kv.first);
    if (i < size() && !m_cmp(kv.first, m_items[i].first))
      return Pair<iterator, bool>(begin() + i, false);
    Expected<Stored *, CommonError> r = m_items.insert(m_items.begin() + i, kv);
    if (!r)
      return MakeUnexpected(r.error());
    return Pair<iterator, bool>(begin() + i, true);
  }
  VAPOR_NODISCARD InsertResult insert(Stored &&kv) {
    size_t i = LowerIndex(kv.first);
    if (i < size() && !m_cmp(kv.first, m_items[i].first))
      return Pair<iterator, bool>(begin() + i, false);
    Expected<Stored *, CommonError> r =
        m_items.insert(m_items.begin() + i, Move(kv));
    if (!r)
      return MakeUnexpected(r.error());
    return Pair<iterator, bool>(begin() + i, true);
  }

  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    Stored kv(Forward<Args>(args)...);
    return insert(Move(kv));
  }

  // Value is only constructed when the key is absent.
  template <typename... Args>
  VAPOR_NODISCARD InsertResult try_emplace(const K &key, Args &&...args) {
    size_t i = LowerIndex(key);
    if (i < size() && !m_cmp(key, m_items[i].first))
      return Pair<iterator, bool>(begin() + i, false);
    return insert(Stored(key, V(Forward<Args>(args)...)));
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult try_emplace(K &&key, Args &&...args) {
    size_t i = LowerIndex(key);
    if (i < size() && !m_cmp(key, m_items[i].first))
      return Pair<iterator, bool>(begin() + i, false);
    return insert(Stored(Move(key), V(Forward<Args>(args)...)));
  }

  template <typename M>
  VAPOR_NODISCARD InsertResult insert_or_assign(const K &key, M &&value) {
    size_t i = FindIndex(key);
    if (i != size()) {
      m_items[i].second = Forward<M>(value);
      return Pair<iterator, bool>(begin() + i, false);
    }
    return insert(Stored(key, V(Forward<M>(value))));
  }
  template <typename M>
  VAPOR_NODISCARD InsertResult insert_or_assign(K &&key, M &&value) {
    size_t i = FindIndex(key);
    if (i != size()) {
      m_items[i].second = Forward<M>(value);
      return Pair<iterator, bool>(begin() + i, false);
    }
    return insert(Stored(Move(key), V(Forward<M>(value))));
  }

  size_type erase(const K &key) noexcept {
    size_t i = FindIndex(key);
    if (i == size())
      return 0;
    m_items.erase(m_items.begin() + i);
    return 1;
  }
  // Returns the iterator following the erased element.
  iterator erase(const_iterator pos) noexcept {
    size_t i = static_cast<size_t>(pos - begin());
    m_items.erase(m_items.begin() + i);
    return begin() + i;
  }

  void clear() noexcept { m_items.clear(); }
  void swap(FlatMap &o) noexcept {
    FlatMap tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }
};

} // namespace vapor
