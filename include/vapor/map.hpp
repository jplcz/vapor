#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/detail/avl_tree.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>
#include <vapor/reference_wrapper.hpp>

// Map<K, V>: an ordered dictionary stored as a balanced (AVL) binary search
// tree, modelled on std::map.
//
// Why: FlatMap keeps one sorted array, so inserting or erasing in the middle
// is O(n) and invalidates every iterator. Map pays one allocation per element
// but inserts, erases and looks up in O(log n) regardless of size, and
// elements never move: iterators and references stay valid until that very
// element is erased. Choose Map for large, frequently modified ordered
// data; FlatMap for small or read-mostly data; HashMap when order does not
// matter.
//
// How:
//
//   Map<int, String> m;
//   auto r = m.try_emplace(3, ...);   // Expected<Pair<iterator, bool>, ...>
//   auto it = m.find(3);              // end() when absent
//   m.at(3);                          // aborts when absent
//   m.checked_at(3);                  // Expected<ReferenceWrapper<V>, ...>
//   for (auto &kv : m) ...            // ascending key order
//   m.lower_bound(k), m.upper_bound(k), m.equal_range(k)
//
// Iterators are bidirectional (++ and --). Same rules as the other vapor
// containers: no exceptions, so anything that allocates returns Expected and
// leaves the map unchanged on failure; there is no operator[]; copying is the
// explicit fallible Clone(). Keys are immutable through iterators, values are
// mutable. Not thread-safe.

namespace vapor {

namespace detail {
template <typename K, typename V> struct MapKeyOf {
  typedef K key_type;
  const K &operator()(const Pair<const K, V> &kv) const noexcept {
    return kv.first;
  }
};
} // namespace detail

template <typename K, typename V, typename Cmp = Less<K>> class Map {
public:
  typedef K key_type;
  typedef V mapped_type;
  typedef Pair<const K, V> value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef Cmp key_compare;
  typedef value_type &reference;
  typedef const value_type &const_reference;

private:
  typedef detail::AvlTree<value_type, detail::MapKeyOf<K, V>, Cmp> Tree;
  Tree m_tree;

public:
  typedef typename Tree::iterator iterator;
  typedef typename Tree::const_iterator const_iterator;
  typedef Expected<Pair<iterator, bool>, CommonError> InsertResult;

private:
  InsertResult
  Wrap(Expected<Pair<detail::AvlNodeBase *, bool>, CommonError> r) {
    if (!r)
      return MakeUnexpected(r.error());
    return Pair<iterator, bool>(m_tree.MakeIterator(r.value().first),
                                r.value().second);
  }

public:
  explicit Map(Allocator &alloc = DefaultAllocator::GetDefault(),
               const Cmp &cmp = Cmp()) noexcept
      : m_tree(alloc, cmp) {}
  Map(Map &&) = default;
  Map &operator=(Map &&) = default;
  Map(const Map &) = delete;
  Map &operator=(const Map &) = delete;

  VAPOR_NODISCARD Expected<Map, CommonError> Clone() const {
    Map out(m_tree.get_allocator(), m_tree.key_comp());
    for (const_iterator it = begin(); it != end(); ++it) {
      Expected<Pair<iterator, bool>, CommonError> r =
          out.insert(value_type(it->first, it->second));
      if (!r)
        return MakeUnexpected(r.error());
    }
    return out;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept {
    return m_tree.get_allocator();
  }
  VAPOR_NODISCARD key_compare key_comp() const { return m_tree.key_comp(); }
  VAPOR_NODISCARD size_type size() const noexcept { return m_tree.size(); }
  VAPOR_NODISCARD bool empty() const noexcept { return m_tree.empty(); }

  VAPOR_NODISCARD iterator begin() noexcept { return m_tree.begin(); }
  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return m_tree.begin();
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return begin(); }
  VAPOR_NODISCARD iterator end() noexcept { return m_tree.end(); }
  VAPOR_NODISCARD const_iterator end() const noexcept { return m_tree.end(); }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return end(); }

  VAPOR_NODISCARD iterator find(const K &key) noexcept {
    return m_tree.MakeIterator(m_tree.Find(key));
  }
  VAPOR_NODISCARD const_iterator find(const K &key) const noexcept {
    return m_tree.MakeIterator(m_tree.Find(key));
  }
  VAPOR_NODISCARD bool contains(const K &key) const noexcept {
    return m_tree.Find(key) != nullptr;
  }
  VAPOR_NODISCARD size_type count(const K &key) const noexcept {
    return contains(key) ? 1 : 0;
  }
  VAPOR_NODISCARD iterator lower_bound(const K &key) noexcept {
    return m_tree.MakeIterator(m_tree.LowerBound(key));
  }
  VAPOR_NODISCARD const_iterator lower_bound(const K &key) const noexcept {
    return m_tree.MakeIterator(m_tree.LowerBound(key));
  }
  VAPOR_NODISCARD iterator upper_bound(const K &key) noexcept {
    return m_tree.MakeIterator(m_tree.UpperBound(key));
  }
  VAPOR_NODISCARD const_iterator upper_bound(const K &key) const noexcept {
    return m_tree.MakeIterator(m_tree.UpperBound(key));
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
    iterator it = find(key);
    if (it == end())
      abort();
    return it->second;
  }
  VAPOR_NODISCARD const V &at(const K &key) const noexcept {
    const_iterator it = find(key);
    if (it == end())
      abort();
    return it->second;
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<V>, CommonError>
  checked_at(const K &key) noexcept {
    iterator it = find(key);
    if (it == end())
      return MakeUnexpected(CommonError::ValueNotFound);
    return ReferenceWrapper<V>(it->second);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const V>, CommonError>
  checked_at(const K &key) const noexcept {
    const_iterator it = find(key);
    if (it == end())
      return MakeUnexpected(CommonError::ValueNotFound);
    return ReferenceWrapper<const V>(it->second);
  }

  // --- modification
  // Insertions return {iterator to the element, true if inserted}; for an
  // existing key nothing changes and `false` is returned.

  VAPOR_NODISCARD InsertResult insert(const value_type &kv) {
    return Wrap(m_tree.InsertWith(kv.first, [&kv](void *mem) {
      detail::ConstructAt<value_type>(mem, kv);
    }));
  }
  VAPOR_NODISCARD InsertResult insert(value_type &&kv) {
    return Wrap(m_tree.InsertWith(kv.first, [&kv](void *mem) {
      detail::ConstructAt<value_type>(mem, Move(kv));
    }));
  }

  // Builds the pair first, so on a duplicate key it is constructed and
  // discarded. Prefer try_emplace.
  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    value_type kv(Forward<Args>(args)...);
    return insert(Move(kv));
  }

  // The value is only constructed when the key is absent.
  template <typename... Args>
  VAPOR_NODISCARD InsertResult try_emplace(const K &key, Args &&...args) {
    return Wrap(m_tree.InsertWith(key, [&key, &args...](void *mem) {
      detail::ConstructAt<value_type>(mem, key, V(Forward<Args>(args)...));
    }));
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult try_emplace(K &&key, Args &&...args) {
    return Wrap(m_tree.InsertWith(key, [&key, &args...](void *mem) {
      detail::ConstructAt<value_type>(mem, Move(key),
                                      V(Forward<Args>(args)...));
    }));
  }

  template <typename M>
  VAPOR_NODISCARD InsertResult insert_or_assign(const K &key, M &&value) {
    iterator it = find(key);
    if (it != end()) {
      it->second = Forward<M>(value);
      return Pair<iterator, bool>(it, false);
    }
    return try_emplace(key, Forward<M>(value));
  }
  template <typename M>
  VAPOR_NODISCARD InsertResult insert_or_assign(K &&key, M &&value) {
    iterator it = find(key);
    if (it != end()) {
      it->second = Forward<M>(value);
      return Pair<iterator, bool>(it, false);
    }
    return try_emplace(Move(key), Forward<M>(value));
  }

  size_type erase(const K &key) noexcept {
    detail::AvlNodeBase *n = m_tree.Find(key);
    if (!n)
      return 0;
    m_tree.EraseNode(n);
    return 1;
  }
  // Returns the iterator following the erased element.
  iterator erase(const_iterator pos) noexcept {
    detail::AvlNodeBase *n = Tree::NodeOf(pos);
    detail::AvlNodeBase *next = Tree::Successor(n);
    m_tree.EraseNode(n);
    return m_tree.MakeIterator(next);
  }

  void clear() noexcept { m_tree.clear(); }
  void swap(Map &o) noexcept { m_tree.swap(o.m_tree); }

  // Test hook: checks the AVL balance and link invariants.
  VAPOR_NODISCARD bool CheckInvariants() const noexcept {
    return m_tree.CheckInvariants();
  }
};

} // namespace vapor
