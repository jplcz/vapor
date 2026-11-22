#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/detail/avl_tree.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>

// Set<K>: an ordered set of unique keys stored as a balanced (AVL) tree,
// modelled on std::set. It is to Map what std::set is to std::map.
//
// Why: see Map. Use Set when you need sorted iteration plus fast insert and
// erase at any size, and stable references to elements. For small or
// read-mostly data prefer FlatSet; when order is irrelevant use HashSet.
//
// How:
//
//   Set<int> s;
//   auto r = s.insert(5);     // Expected<Pair<iterator, bool>, ...>
//   s.contains(5);
//   for (int k : s) ...       // ascending order
//   s.lower_bound(k), s.upper_bound(k), s.erase(k)
//
// Elements are immutable through iterators (both iterator types are const).
// Anything that allocates returns Expected and leaves the set unchanged on
// failure; copying is the explicit fallible Clone(). Not thread-safe.

namespace vapor {

namespace detail {
template <typename K> struct SetKeyOf {
  typedef K key_type;
  const K &operator()(const K &k) const noexcept { return k; }
};
} // namespace detail

template <typename K, typename Cmp = Less<K>> class Set {
public:
  typedef K key_type;
  typedef K value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef Cmp key_compare;
  typedef const K &reference;
  typedef const K &const_reference;

private:
  typedef detail::AvlTree<K, detail::SetKeyOf<K>, Cmp> Tree;
  Tree m_tree;

public:
  typedef typename Tree::const_iterator iterator;
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
  explicit Set(Allocator &alloc = DefaultAllocator::GetDefault(),
               const Cmp &cmp = Cmp()) noexcept
      : m_tree(alloc, cmp) {}
  Set(Set &&) = default;
  Set &operator=(Set &&) = default;
  Set(const Set &) = delete;
  Set &operator=(const Set &) = delete;

  VAPOR_NODISCARD Expected<Set, CommonError> Clone() const {
    Set out(m_tree.get_allocator(), m_tree.key_comp());
    for (const_iterator it = begin(); it != end(); ++it) {
      Expected<Pair<iterator, bool>, CommonError> r = out.insert(*it);
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

  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return m_tree.begin();
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return begin(); }
  VAPOR_NODISCARD const_iterator end() const noexcept { return m_tree.end(); }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return end(); }

  VAPOR_NODISCARD const_iterator find(const K &key) const noexcept {
    return m_tree.MakeIterator(m_tree.Find(key));
  }
  VAPOR_NODISCARD bool contains(const K &key) const noexcept {
    return m_tree.Find(key) != nullptr;
  }
  VAPOR_NODISCARD size_type count(const K &key) const noexcept {
    return contains(key) ? 1 : 0;
  }
  VAPOR_NODISCARD const_iterator lower_bound(const K &key) const noexcept {
    return m_tree.MakeIterator(m_tree.LowerBound(key));
  }
  VAPOR_NODISCARD const_iterator upper_bound(const K &key) const noexcept {
    return m_tree.MakeIterator(m_tree.UpperBound(key));
  }
  VAPOR_NODISCARD Pair<const_iterator, const_iterator>
  equal_range(const K &key) const noexcept {
    return Pair<const_iterator, const_iterator>(lower_bound(key),
                                                upper_bound(key));
  }

  VAPOR_NODISCARD InsertResult insert(const K &key) {
    return Wrap(m_tree.InsertWith(
        key, [&key](void *mem) { detail::ConstructAt<K>(mem, key); }));
  }
  VAPOR_NODISCARD InsertResult insert(K &&key) {
    return Wrap(m_tree.InsertWith(
        key, [&key](void *mem) { detail::ConstructAt<K>(mem, Move(key)); }));
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    K key(Forward<Args>(args)...);
    return insert(Move(key));
  }

  size_type erase(const K &key) noexcept {
    detail::AvlNodeBase *n = m_tree.Find(key);
    if (!n)
      return 0;
    m_tree.EraseNode(n);
    return 1;
  }
  // Returns the iterator following the erased element.
  const_iterator erase(const_iterator pos) noexcept {
    detail::AvlNodeBase *n = Tree::NodeOf(pos);
    detail::AvlNodeBase *next = Tree::Successor(n);
    m_tree.EraseNode(n);
    return m_tree.MakeIterator(next);
  }

  void clear() noexcept { m_tree.clear(); }
  void swap(Set &o) noexcept { m_tree.swap(o.m_tree); }

  // Test hook: checks the AVL balance and link invariants.
  VAPOR_NODISCARD bool CheckInvariants() const noexcept {
    return m_tree.CheckInvariants();
  }
};

} // namespace vapor
