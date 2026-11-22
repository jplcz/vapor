#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/detail/hash_table.hpp>
#include <vapor/expected.hpp>
#include <vapor/hash.hpp>
#include <vapor/pair.hpp>
#include <vapor/reference_wrapper.hpp>

// HashMap<K, V>: a dictionary that finds a value by key in constant average
// time, modelled on std::unordered_map.
//
// Why: searching a Vector for a key is O(n); a sorted array (FlatMap) is
// O(log n) to search but O(n) to insert. A hash table does both in O(1) on
// average, which is what you want for caches, symbol tables, id -> object
// lookups and counting words. Keys must be hashable (see hash.hpp, which
// covers integers, pointers, enums, StringView and String and can be
// extended) and comparable with ==.
//
// How:
//
//   HashMap<int, String> names;
//   Expected<Pair<HashMap<int, String>::iterator, bool>, CommonError> r =
//       names.try_emplace(7, ...);         // OutOfMemory if it cannot grow
//   auto it = names.find(7);               // iterator, == end() when absent
//   if (it != names.end()) use(it->second);
//   names.at(7);                           // reference, aborts when absent
//   names.checked_at(7);                   // Expected<ReferenceWrapper<V>>
//   for (auto &kv : names) kv.first, kv.second   // unspecified order
//
// Differences from std::unordered_map, all because the library has no
// exceptions:
//  * everything that may allocate returns Expected<..., CommonError>, and the
//    map is left unchanged on failure;
//  * there is no operator[]: it would have to insert, which can fail. Use
//    try_emplace(key) (then ->first/->second) or insert_or_assign;
//  * copying is explicit and fallible: use Clone();
//  * at() aborts on a missing key; checked_at() reports ValueNotFound.
//
// Behaviour worth knowing:
//  * Iteration order is unspecified and changes when the table grows.
//  * Inserting may rebuild the table, which invalidates ALL iterators and
//    references. Erasing invalidates only the erased element.
//  * Keys cannot be modified through iterators (value_type is
//    Pair<const K, V>); values can.
//  * Lookup with another key type (e.g. a StringView in a HashMap<String, V>)
//    works when the hasher and comparator are transparent:
//    HashMap<String, V, Hash<>, EqualTo<>>.
//  * Not thread-safe: guard concurrent access yourself.

namespace vapor {

template <typename K, typename V, typename H = Hash<K>, typename E = EqualTo<K>>
class HashMap {
public:
  typedef K key_type;
  typedef V mapped_type;
  typedef Pair<const K, V> value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef H hasher;
  typedef E key_equal;
  typedef value_type &reference;
  typedef const value_type &const_reference;

private:
  typedef Pair<K, V> Stored;

  struct KeyOf {
    typedef K key_type;
    const K &operator()(const Stored &s) const noexcept { return s.first; }
  };

  typedef detail::HashTable<Stored, value_type, KeyOf, H, E> Table;
  Table m_table;

  template <typename Q>
  struct EnableHetero
      : EnableIf<detail::IsTransparent<H>::value &&
                     detail::IsTransparent<E>::value && !IsSame<Q, K>::value,
                 int> {};

public:
  typedef typename Table::iterator iterator;
  typedef typename Table::const_iterator const_iterator;
  typedef Expected<Pair<iterator, bool>, CommonError> InsertResult;

  explicit HashMap(Allocator &alloc = DefaultAllocator::GetDefault(),
                   const H &h = H(), const E &e = E())
      : m_table(alloc, h, e) {}

  HashMap(HashMap &&) = default;
  HashMap &operator=(HashMap &&) = default;
  HashMap(const HashMap &) = delete;
  HashMap &operator=(const HashMap &) = delete;

  // Explicit, fallible copy (requires copyable K and V).
  VAPOR_NODISCARD Expected<HashMap, CommonError> Clone() const {
    HashMap out(m_table.get_allocator(), m_table.hash_function(),
                m_table.key_eq());
    Expected<void, CommonError> r = out.reserve(size());
    if (!r)
      return MakeUnexpected(r.error());
    m_table.ForEachStored([&out](const Stored &s) {
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
  VAPOR_NODISCARD size_type max_size() const noexcept {
    return VAPOR_SIZE_MAX / sizeof(Stored);
  }
  VAPOR_NODISCARD size_type bucket_count() const noexcept {
    return m_table.bucket_count();
  }
  VAPOR_NODISCARD float load_factor() const noexcept {
    return m_table.load_factor();
  }

  // Makes room for `n` elements so that inserting them cannot fail or
  // rebuild the table.
  Expected<void, CommonError> reserve(size_type n) noexcept {
    return m_table.reserve(n);
  }

  VAPOR_NODISCARD iterator begin() noexcept { return m_table.begin(); }
  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return m_table.begin();
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept {
    return m_table.begin();
  }
  VAPOR_NODISCARD iterator end() noexcept { return m_table.end(); }
  VAPOR_NODISCARD const_iterator end() const noexcept { return m_table.end(); }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return m_table.end(); }

  // --- lookup

  VAPOR_NODISCARD iterator find(const K &key) noexcept {
    return m_table.MakeIterator(m_table.FindIndex(key));
  }
  VAPOR_NODISCARD const_iterator find(const K &key) const noexcept {
    return m_table.MakeIterator(m_table.FindIndex(key));
  }
  template <typename Q, typename EnableHetero<Q>::type = 0>
  VAPOR_NODISCARD iterator find(const Q &key) noexcept {
    return m_table.MakeIterator(m_table.FindIndexAs(key));
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
  template <typename Q, typename EnableHetero<Q>::type = 0>
  VAPOR_NODISCARD size_type count(const Q &key) const noexcept {
    return contains(key) ? 1 : 0;
  }

  // Reference to the mapped value; aborts when the key is absent.
  VAPOR_NODISCARD V &at(const K &key) noexcept {
    size_t i = m_table.FindIndex(key);
    if (i == Table::npos)
      abort();
    return m_table.At(i).second;
  }
  VAPOR_NODISCARD const V &at(const K &key) const noexcept {
    size_t i = m_table.FindIndex(key);
    if (i == Table::npos)
      abort();
    return m_table.At(i).second;
  }

  // Non-trapping variant of at(): ValueNotFound when the key is absent.
  VAPOR_NODISCARD Expected<ReferenceWrapper<V>, CommonError>
  checked_at(const K &key) noexcept {
    size_t i = m_table.FindIndex(key);
    if (i == Table::npos)
      return MakeUnexpected(CommonError::ValueNotFound);
    return ReferenceWrapper<V>(m_table.At(i).second);
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const V>, CommonError>
  checked_at(const K &key) const noexcept {
    size_t i = m_table.FindIndex(key);
    if (i == Table::npos)
      return MakeUnexpected(CommonError::ValueNotFound);
    return ReferenceWrapper<const V>(m_table.At(i).second);
  }

  // --- modification
  // Every insertion returns {iterator to the element, true if inserted}; when
  // the key already exists nothing changes and `false` is returned.

  VAPOR_NODISCARD InsertResult insert(const Stored &kv) {
    return InsertImpl(
        kv.first, [&kv](void *mem) { detail::ConstructAt<Stored>(mem, kv); });
  }
  VAPOR_NODISCARD InsertResult insert(Stored &&kv) {
    return InsertImpl(kv.first, [&kv](void *mem) {
      detail::ConstructAt<Stored>(mem, Move(kv));
    });
  }

  // Builds the key/value pair from `args` first, so on a duplicate key the
  // pair is constructed and then discarded. Prefer try_emplace.
  template <typename... Args>
  VAPOR_NODISCARD InsertResult emplace(Args &&...args) {
    Stored kv(Forward<Args>(args)...);
    return insert(Move(kv));
  }

  // Inserts key -> V(args...) only if the key is absent; unlike emplace the
  // value is not constructed for an existing key.
  template <typename... Args>
  VAPOR_NODISCARD InsertResult try_emplace(const K &key, Args &&...args) {
    return InsertImpl(key, [&key, &args...](void *mem) {
      detail::ConstructAt<Stored>(mem, key, V(Forward<Args>(args)...));
    });
  }
  template <typename... Args>
  VAPOR_NODISCARD InsertResult try_emplace(K &&key, Args &&...args) {
    return InsertImpl(key, [&key, &args...](void *mem) {
      detail::ConstructAt<Stored>(mem, Move(key), V(Forward<Args>(args)...));
    });
  }

  // Inserts, or overwrites the value of an existing key. `second` of the
  // result tells whether a new element was created.
  template <typename M>
  VAPOR_NODISCARD InsertResult insert_or_assign(const K &key, M &&value) {
    InsertResult r = InsertImpl(key, [&key, &value](void *mem) {
      detail::ConstructAt<Stored>(mem, key, V(Forward<M>(value)));
    });
    if (r && !r.value().second)
      r.value().first->second = Forward<M>(value);
    return r;
  }
  template <typename M>
  VAPOR_NODISCARD InsertResult insert_or_assign(K &&key, M &&value) {
    InsertResult r = InsertImpl(key, [&key, &value](void *mem) {
      detail::ConstructAt<Stored>(mem, Move(key), V(Forward<M>(value)));
    });
    if (r && !r.value().second)
      r.value().first->second = Forward<M>(value);
    return r;
  }

  // Removes the element with this key; returns how many were removed (0/1).
  size_type erase(const K &key) noexcept {
    size_t i = m_table.FindIndex(key);
    if (i == Table::npos)
      return 0;
    m_table.EraseIndex(i);
    return 1;
  }
  // Removes the element at `pos` (must be valid, not end()); returns the
  // iterator to the next element.
  iterator erase(const_iterator pos) noexcept {
    size_t i = m_table.IndexOf(pos);
    m_table.EraseIndex(i);
    iterator next = m_table.MakeIterator(i);
    ++next;
    return next;
  }

  void clear() noexcept { m_table.clear(); }
  void swap(HashMap &o) noexcept { m_table.swap(o.m_table); }

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
