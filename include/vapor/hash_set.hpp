#pragma once
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/detail/hash_table.hpp>
#include <vapor/expected.hpp>
#include <vapor/hash.hpp>
#include <vapor/pair.hpp>

// HashSet<K>: a collection of unique keys with constant average time
// membership tests, modelled on std::unordered_set.
//
// Why: "have I seen this before?" (de-duplication, visited sets, allow
// lists) is O(n) on a Vector and O(1) on average here.
//
// How:
//
//   HashSet<int> seen;
//   auto r = seen.insert(5);        // Expected<Pair<iterator, bool>, ...>
//   if (r && r.value().second) { /* newly added */ }
//   seen.contains(5);
//   seen.erase(5);
//
// It follows the same rules as HashMap (see hash_map.hpp): allocation can
// fail so insertions return Expected and leave the set unchanged on failure,
// copying is the explicit fallible Clone(), inserting invalidates all
// iterators, iteration order is unspecified, and it is not thread-safe.
// Elements are immutable through iterators (changing a key would corrupt the
// table).

namespace vapor {

template <typename K, typename H = Hash<K>, typename E = EqualTo<K>>
class HashSet {
public:
  typedef K key_type;
  typedef K value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef H hasher;
  typedef E key_equal;
  typedef const K &reference;
  typedef const K &const_reference;

private:
  struct KeyOf {
    typedef K key_type;
    const K &operator()(const K &s) const noexcept { return s; }
  };

  typedef detail::HashTable<K, const K, KeyOf, H, E> Table;
  Table m_table;

  template <typename Q>
  struct EnableHetero
      : EnableIf<detail::IsTransparent<H>::value &&
                     detail::IsTransparent<E>::value && !IsSame<Q, K>::value,
                 int> {};

public:
  typedef typename Table::const_iterator iterator;
  typedef typename Table::const_iterator const_iterator;
  typedef Expected<Pair<iterator, bool>, CommonError> InsertResult;

  explicit HashSet(Allocator &alloc = DefaultAllocator::GetDefault(),
                   const H &h = H(), const E &e = E())
      : m_table(alloc, h, e) {}

  HashSet(HashSet &&) = default;
  HashSet &operator=(HashSet &&) = default;
  HashSet(const HashSet &) = delete;
  HashSet &operator=(const HashSet &) = delete;

  VAPOR_NODISCARD Expected<HashSet, CommonError> Clone() const {
    HashSet out(m_table.get_allocator(), m_table.hash_function(),
                m_table.key_eq());
    Expected<void, CommonError> r = out.reserve(size());
    if (!r)
      return MakeUnexpected(r.error());
    m_table.ForEachStored([&out](const K &s) {
      // Cannot fail: room for size() elements was reserved above.
      InsertResult ignored = out.insert(s);
      (void)ignored;
    });
    return out;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept {
    return m_table.get_allocator();
  }
  VAPOR_NODISCARD hasher hash_function() const {
    return m_table.hash_function();
  }
  VAPOR_NODISCARD key_equal key_eq() const { return m_table.key_eq(); }

  VAPOR_NODISCARD size_type size() const noexcept { return m_table.size(); }
  VAPOR_NODISCARD bool empty() const noexcept { return m_table.empty(); }
  VAPOR_NODISCARD size_type bucket_count() const noexcept {
    return m_table.bucket_count();
  }
  VAPOR_NODISCARD float load_factor() const noexcept {
    return m_table.load_factor();
  }

  Expected<void, CommonError> reserve(size_type n) noexcept {
    return m_table.reserve(n);
  }

  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return m_table.begin();
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept {
    return m_table.begin();
  }
  VAPOR_NODISCARD const_iterator end() const noexcept { return m_table.end(); }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return m_table.end(); }

  VAPOR_NODISCARD const_iterator find(const K &key) const noexcept {
    return m_table.MakeIterator(m_table.FindIndex(key));
  }
  template <typename Q, typename EnableHetero<Q>::type = 0>
  VAPOR_NODISCARD const_iterator find(const Q &key) const noexcept {
    return m_table.MakeIterator(m_table.FindIndexAs(key));
  }
  VAPOR_NODISCARD bool contains(const K &key) const noexcept {
    return m_table.FindIndex(key) != Table::npos;
  }
  template <typename Q, typename EnableHetero<Q>::type = 0>
  VAPOR_NODISCARD bool contains(const Q &key) const noexcept {
    return m_table.FindIndexAs(key) != Table::npos;
  }
  VAPOR_NODISCARD size_type count(const K &key) const noexcept {
    return contains(key) ? 1 : 0;
  }

  VAPOR_NODISCARD InsertResult insert(const K &key) {
    return InsertImpl(key,
                      [&key](void *mem) { detail::ConstructAt<K>(mem, key); });
  }
  VAPOR_NODISCARD InsertResult insert(K &&key) {
    return InsertImpl(
        key, [&key](void *mem) { detail::ConstructAt<K>(mem, Move(key)); });
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    K key(Forward<Args>(args)...);
    return insert(Move(key));
  }

  size_type erase(const K &key) noexcept {
    size_t i = m_table.FindIndex(key);
    if (i == Table::npos)
      return 0;
    m_table.EraseIndex(i);
    return 1;
  }
  iterator erase(const_iterator pos) noexcept {
    size_t i = m_table.IndexOf(pos);
    m_table.EraseIndex(i);
    iterator next = m_table.MakeIterator(i);
    ++next;
    return next;
  }

  void clear() noexcept { m_table.clear(); }
  void swap(HashSet &o) noexcept { m_table.swap(o.m_table); }

private:
  template <typename Maker> InsertResult InsertImpl(const K &key, Maker make) {
    Expected<Pair<size_t, bool>, CommonError> r = m_table.InsertWith(key, make);
    if (!r)
      return MakeUnexpected(r.error());
    return Pair<iterator, bool>(m_table.MakeIterator(r.value().first),
                                r.value().second);
  }
};

} // namespace vapor
