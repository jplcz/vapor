#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/detail/avl_tree.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>
#include <vapor/set.hpp>

// MultiSet<K>: an ordered collection that keeps duplicates, modelled on
// std::multiset.
//
// Why: Set stores each key once. A MultiSet keeps every inserted element in
// sorted order, so it answers "how many 5s are there" (`count`) and walks
// equal elements together (`equal_range`). Typical uses: histograms,
// scheduling queues where several tasks share a priority, and keeping a
// sorted running list with cheap insertion and removal.
//
// How:
//
//   MultiSet<int> s;
//   auto r = s.insert(5);            // Expected<iterator, ...>, always adds
//   s.count(5);
//   auto range = s.equal_range(5);
//   s.erase(5);                      // removes all fives, returns the count
//
// Same rules as Set: elements are immutable through iterators, insertion
// returns Expected and leaves the set unchanged on failure, copying is Clone().

namespace vapor {

template <typename K, typename Cmp = Less<K>> class MultiSet {
public:
  typedef K key_type;
  typedef K value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef Cmp key_compare;

private:
  typedef detail::AvlTree<K, detail::SetKeyOf<K>, Cmp> Tree;
  Tree m_tree;

public:
  typedef typename Tree::const_iterator iterator;
  typedef typename Tree::const_iterator const_iterator;
  typedef Expected<iterator, CommonError> InsertResult;

private:
  InsertResult Wrap(Expected<detail::AvlNodeBase *, CommonError> r) {
    if (!r)
      return MakeUnexpected(r.error());
    return iterator(m_tree.MakeIterator(r.value()));
  }

public:
  explicit MultiSet(Allocator &alloc = DefaultAllocator::GetDefault(),
                    const Cmp &cmp = Cmp()) noexcept
      : m_tree(alloc, cmp) {}
  MultiSet(MultiSet &&) = default;
  MultiSet &operator=(MultiSet &&) = default;
  MultiSet(const MultiSet &) = delete;
  MultiSet &operator=(const MultiSet &) = delete;

  VAPOR_NODISCARD Expected<MultiSet, CommonError> Clone() const {
    MultiSet out(m_tree.get_allocator(), m_tree.key_comp());
    for (const_iterator it = begin(); it != end(); ++it) {
      Expected<iterator, CommonError> r = out.insert(*it);
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
    size_type n = 0;
    const_iterator last = upper_bound(key);
    for (const_iterator it = lower_bound(key); it != last; ++it)
      ++n;
    return n;
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

  // Always adds the element (after existing equal ones).
  VAPOR_NODISCARD InsertResult insert(const K &key) {
    return Wrap(m_tree.InsertEqual(
        key, [&key](void *mem) { detail::ConstructAt<K>(mem, key); }));
  }
  VAPOR_NODISCARD InsertResult insert(K &&key) {
    return Wrap(m_tree.InsertEqual(
        key, [&key](void *mem) { detail::ConstructAt<K>(mem, Move(key)); }));
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    K key(Forward<Args>(args)...);
    return insert(Move(key));
  }

  // Removes every element equal to `key`; returns how many.
  size_type erase(const K &key) noexcept {
    size_type n = 0;
    detail::AvlNodeBase *last = m_tree.UpperBound(key);
    detail::AvlNodeBase *cur = m_tree.LowerBound(key);
    while (cur != last) {
      detail::AvlNodeBase *next = Tree::Successor(cur);
      m_tree.EraseNode(cur);
      cur = next;
      ++n;
    }
    return n;
  }
  const_iterator erase(const_iterator pos) noexcept {
    detail::AvlNodeBase *n = Tree::NodeOf(pos);
    detail::AvlNodeBase *next = Tree::Successor(n);
    m_tree.EraseNode(n);
    return m_tree.MakeIterator(next);
  }

  void clear() noexcept { m_tree.clear(); }
  void swap(MultiSet &o) noexcept { m_tree.swap(o.m_tree); }

  VAPOR_NODISCARD bool CheckInvariants() const noexcept {
    return m_tree.CheckInvariants();
  }
};

} // namespace vapor
