#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/detail/avl_tree.hpp>
#include <vapor/expected.hpp>
#include <vapor/map.hpp>
#include <vapor/pair.hpp>

// MultiMap<K, V>: an ordered map that may hold several values under the same
// key, modelled on std::multimap.
//
// Why: Map keeps one value per key. Many real relations are one-to-many: all
// events at the same timestamp, all files with the same extension, every
// handler registered for a message type. MultiMap stores them in the same
// balanced tree, in key order, with equal keys kept in insertion order.
//
// How:
//
//   MultiMap<int, String> m;
//   auto r = m.emplace(3, ...);          // Expected<iterator, ...>, always
//   adds auto range = m.equal_range(3);       // all entries with key 3
//   m.count(3);                          // how many
//   m.erase(3);                          // removes all of them, returns count
//   m.find(3);                           // the first one, or end()
//
// There is no operator[] or at(): with several values per key neither has a
// single answer. Everything else follows Map: insertion returns Expected and
// leaves the container unchanged on failure, iterators are bidirectional and
// stay valid until their element is erased, copying is Clone().

namespace vapor {

template <typename K, typename V, typename Cmp = Less<K>> class MultiMap {
public:
  typedef K key_type;
  typedef V mapped_type;
  typedef Pair<const K, V> value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef Cmp key_compare;

private:
  typedef detail::AvlTree<value_type, detail::MapKeyOf<K, V>, Cmp> Tree;
  Tree m_tree;

public:
  typedef typename Tree::iterator iterator;
  typedef typename Tree::const_iterator const_iterator;
  typedef Expected<iterator, CommonError> InsertResult;

private:
  InsertResult Wrap(Expected<detail::AvlNodeBase *, CommonError> r) {
    if (!r)
      return MakeUnexpected(r.error());
    return m_tree.MakeIterator(r.value());
  }

public:
  explicit MultiMap(Allocator &alloc = DefaultAllocator::GetDefault(),
                    const Cmp &cmp = Cmp()) noexcept
      : m_tree(alloc, cmp) {}
  MultiMap(MultiMap &&) = default;
  MultiMap &operator=(MultiMap &&) = default;
  MultiMap(const MultiMap &) = delete;
  MultiMap &operator=(const MultiMap &) = delete;

  VAPOR_NODISCARD Expected<MultiMap, CommonError> Clone() const {
    MultiMap out(m_tree.get_allocator(), m_tree.key_comp());
    for (const_iterator it = begin(); it != end(); ++it) {
      Expected<iterator, CommonError> r =
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

  // First entry with this key, or end().
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
    size_type n = 0;
    const_iterator last = upper_bound(key);
    for (const_iterator it = lower_bound(key); it != last; ++it)
      ++n;
    return n;
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

  // Always adds an entry (after existing entries with an equal key).
  VAPOR_NODISCARD InsertResult insert(const value_type &kv) {
    return Wrap(m_tree.InsertEqual(kv.first, [&kv](void *mem) {
      detail::ConstructAt<value_type>(mem, kv);
    }));
  }
  VAPOR_NODISCARD InsertResult insert(value_type &&kv) {
    return Wrap(m_tree.InsertEqual(kv.first, [&kv](void *mem) {
      detail::ConstructAt<value_type>(mem, Move(kv));
    }));
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    value_type kv(Forward<Args>(args)...);
    return insert(Move(kv));
  }

  // Removes every entry with this key; returns how many.
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
  // Returns the iterator following the erased entry.
  iterator erase(const_iterator pos) noexcept {
    detail::AvlNodeBase *n = Tree::NodeOf(pos);
    detail::AvlNodeBase *next = Tree::Successor(n);
    m_tree.EraseNode(n);
    return m_tree.MakeIterator(next);
  }

  void clear() noexcept { m_tree.clear(); }
  void swap(MultiMap &o) noexcept { m_tree.swap(o.m_tree); }

  // Test hook: checks the AVL balance and link invariants.
  VAPOR_NODISCARD bool CheckInvariants() const noexcept {
    return m_tree.CheckInvariants();
  }
};

} // namespace vapor
