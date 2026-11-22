#pragma once
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>
#include <vapor/vector.hpp>

// FlatSet<K>: an ordered set of unique keys stored as one sorted array,
// modelled on std::set / C++23 std::flat_set.
//
// Why: same trade-off as FlatMap (see flat_map.hpp): one allocation, cache
// friendly, ascending iteration, O(log n) lookup, but O(n) insert/erase in
// the middle. Good for small or read-mostly sets and for ordered output.
//
// How:
//
//   FlatSet<int> s;
//   auto r = s.insert(4);        // Expected<Pair<iterator, bool>, ...>
//   s.contains(4); s.erase(4);
//   for (int k : s) ...          // ascending order
//
// No exceptions: insertion returns Expected and leaves the set unchanged on
// failure; copying is the explicit fallible Clone(); inserting or erasing
// invalidates iterators; elements are immutable through iterators.

namespace vapor {

template <typename K, typename Cmp = Less<K>> class FlatSet {
public:
  typedef K key_type;
  typedef K value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef Cmp key_compare;
  typedef const K &reference;
  typedef const K &const_reference;
  typedef const K *iterator;
  typedef const K *const_iterator;
  typedef Expected<Pair<iterator, bool>, CommonError> InsertResult;

private:
  Vector<K> m_items;
  Cmp m_cmp;

  size_t LowerIndex(const K &key) const noexcept {
    size_t lo = 0, hi = m_items.size();
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (m_cmp(m_items[mid], key))
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
      if (!m_cmp(key, m_items[mid]))
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }
  size_t FindIndex(const K &key) const noexcept {
    size_t i = LowerIndex(key);
    if (i < m_items.size() && !m_cmp(key, m_items[i]))
      return i;
    return m_items.size();
  }

public:
  explicit FlatSet(Allocator &alloc = DefaultAllocator::GetDefault(),
                   const Cmp &cmp = Cmp()) noexcept
      : m_items(alloc), m_cmp(cmp) {}

  FlatSet(FlatSet &&) = default;
  FlatSet &operator=(FlatSet &&) = default;
  FlatSet(const FlatSet &) = delete;
  FlatSet &operator=(const FlatSet &) = delete;

  VAPOR_NODISCARD Expected<FlatSet, CommonError> Clone() const {
    Expected<Vector<K>, CommonError> items = m_items.Clone();
    if (!items)
      return MakeUnexpected(items.error());
    FlatSet out(m_items.get_allocator(), m_cmp);
    out.m_items = Move(items.value());
    return out;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept {
    return m_items.get_allocator();
  }
  VAPOR_NODISCARD key_compare key_comp() const { return m_cmp; }
  VAPOR_NODISCARD size_type size() const noexcept { return m_items.size(); }
  VAPOR_NODISCARD bool empty() const noexcept { return m_items.empty(); }
  Expected<void, CommonError> reserve(size_type n) noexcept {
    return m_items.reserve(n);
  }

  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return m_items.begin();
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return begin(); }
  VAPOR_NODISCARD const_iterator end() const noexcept { return m_items.end(); }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return end(); }

  VAPOR_NODISCARD const_iterator find(const K &key) const noexcept {
    return begin() + FindIndex(key);
  }
  VAPOR_NODISCARD bool contains(const K &key) const noexcept {
    return FindIndex(key) != size();
  }
  VAPOR_NODISCARD size_type count(const K &key) const noexcept {
    return contains(key) ? 1 : 0;
  }
  VAPOR_NODISCARD const_iterator lower_bound(const K &key) const noexcept {
    return begin() + LowerIndex(key);
  }
  VAPOR_NODISCARD const_iterator upper_bound(const K &key) const noexcept {
    return begin() + UpperIndex(key);
  }
  VAPOR_NODISCARD Pair<const_iterator, const_iterator>
  equal_range(const K &key) const noexcept {
    return Pair<const_iterator, const_iterator>(lower_bound(key),
                                                upper_bound(key));
  }

  VAPOR_NODISCARD InsertResult insert(const K &key) {
    size_t i = LowerIndex(key);
    if (i < size() && !m_cmp(key, m_items[i]))
      return Pair<iterator, bool>(begin() + i, false);
    Expected<K *, CommonError> r = m_items.insert(m_items.begin() + i, key);
    if (!r)
      return MakeUnexpected(r.error());
    return Pair<iterator, bool>(begin() + i, true);
  }
  VAPOR_NODISCARD InsertResult insert(K &&key) {
    size_t i = LowerIndex(key);
    if (i < size() && !m_cmp(key, m_items[i]))
      return Pair<iterator, bool>(begin() + i, false);
    Expected<K *, CommonError> r =
        m_items.insert(m_items.begin() + i, Move(key));
    if (!r)
      return MakeUnexpected(r.error());
    return Pair<iterator, bool>(begin() + i, true);
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    K key(Forward<Args>(args)...);
    return insert(Move(key));
  }

  size_type erase(const K &key) noexcept {
    size_t i = FindIndex(key);
    if (i == size())
      return 0;
    m_items.erase(m_items.begin() + i);
    return 1;
  }
  iterator erase(const_iterator pos) noexcept {
    size_t i = static_cast<size_t>(pos - begin());
    m_items.erase(m_items.begin() + i);
    return begin() + i;
  }

  void clear() noexcept { m_items.clear(); }
  void swap(FlatSet &o) noexcept {
    FlatSet tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }
};

} // namespace vapor
