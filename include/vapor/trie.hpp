#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/optional.hpp>
#include <vapor/pair.hpp>
#include <vapor/string_view.hpp>
#include <vapor/utility.hpp>
#include <vapor/vector.hpp>

// Trie<V>: a dictionary from byte-string keys to values, stored as a tree
// with one level per character (also called a prefix tree).
//
// Why: a HashMap can only answer "is this exact key present". A Trie also
// answers questions about prefixes, and does so in time proportional to the
// length of the text, not the number of keys:
//   * LongestPrefix("/usr/bin/ls")  - the longest stored key that is a prefix
//     of the text (routing tables, URL or path mounts, keyword matching);
//   * HasPrefix("pri")              - does any key start with this? (auto
//     completion);
//   * ForEach("pri", fn)            - visit every key with that prefix, in
//     ascending byte order.
// Keys that share a beginning also share memory for it.
//
// How:
//
//   Trie<int> t;
//   auto r = t.try_emplace("print", 1);      // Expected<Pair<int*, bool>, ...>
//   t.insert_or_assign("printf", 2);
//   int *v = t.find("print");                // nullptr when absent
//   Trie<int>::PrefixMatch m = t.LongestPrefix("printfoo");
//   if (m.value) { /* key "print" matched, m.length == 5, *m.value == 1 */ }
//   t.ForEach("pri", [](StringView key, int &value) { ... });
//   t.erase("print");
//
// Keys are arbitrary bytes (including NUL and the empty key). Anything that
// allocates returns Expected and leaves the trie unchanged on failure.
// Pointers to values stay valid until the trie next grows (inserting may
// move nodes), so do not keep them across insertions. erase() frees nodes
// that no longer lead to a value for later reuse; the memory itself is
// returned by clear() or destruction. Copying is not provided. Not
// thread-safe, and the ForEach callback must not modify the trie.

namespace vapor {

template <typename V> class Trie {
  typedef uint32_t Index;
  static Index None() noexcept { return 0xFFFFFFFFu; }

  struct Node {
    Index parent;
    Index first_child;
    Index next_sibling; // also links the free list
    unsigned char ch;
    Optional<V> value;
  };

  Vector<Node> m_nodes; // m_nodes[0] is the root once it exists
  Index m_free;
  size_t m_size;

  static unsigned char Byte(char c) noexcept {
    return static_cast<unsigned char>(c);
  }

  Index FindChild(Index parent, unsigned char ch) const noexcept {
    for (Index c = m_nodes[parent].first_child; c != None();
         c = m_nodes[c].next_sibling) {
      if (m_nodes[c].ch == ch)
        return c;
      if (m_nodes[c].ch > ch)
        break; // children are kept sorted
    }
    return None();
  }

  Index Walk(StringView key) const noexcept {
    if (m_nodes.empty())
      return None();
    Index cur = 0;
    for (size_t i = 0; i < key.size() && cur != None(); ++i)
      cur = FindChild(cur, Byte(key[i]));
    return cur;
  }

  Expected<Index, CommonError> AllocNode(Index parent, unsigned char ch) {
    Index idx;
    if (m_free != None()) {
      idx = m_free;
      m_free = m_nodes[idx].next_sibling;
    } else {
      if (m_nodes.size() >= static_cast<size_t>(None()))
        return MakeUnexpected(CommonError::CapacityExceeded);
      Node blank;
      blank.parent = None();
      blank.first_child = None();
      blank.next_sibling = None();
      blank.ch = 0;
      Expected<void, CommonError> r = m_nodes.push_back(Move(blank));
      if (!r)
        return MakeUnexpected(r.error());
      idx = static_cast<Index>(m_nodes.size() - 1);
    }
    Node &n = m_nodes[idx];
    n.parent = parent;
    n.first_child = None();
    n.next_sibling = None();
    n.ch = ch;
    return idx;
  }

  void LinkChild(Index parent, Index child) noexcept {
    Index *link = &m_nodes[parent].first_child;
    while (*link != None() && m_nodes[*link].ch < m_nodes[child].ch)
      link = &m_nodes[*link].next_sibling;
    m_nodes[child].next_sibling = *link;
    *link = child;
  }

  // Removes `idx` and its ancestors while they hold no value and no children.
  void Prune(Index idx) noexcept {
    while (idx != 0 && idx != None()) {
      Node &n = m_nodes[idx];
      if (n.value.has_value() || n.first_child != None())
        return;
      Index parent = n.parent;
      Index *link = &m_nodes[parent].first_child;
      while (*link != idx)
        link = &m_nodes[*link].next_sibling;
      *link = n.next_sibling;
      n.next_sibling = m_free;
      m_free = idx;
      idx = parent;
    }
  }

  // Finds or creates the node for `key`; on failure removes what it created.
  Expected<Index, CommonError> Ensure(StringView key) {
    if (m_nodes.empty()) {
      Expected<Index, CommonError> root = AllocNode(None(), 0);
      if (!root)
        return MakeUnexpected(root.error());
    }
    Index cur = 0;
    for (size_t i = 0; i < key.size(); ++i) {
      unsigned char ch = Byte(key[i]);
      Index next = FindChild(cur, ch);
      if (next == None()) {
        Expected<Index, CommonError> n = AllocNode(cur, ch);
        if (!n) {
          Prune(cur);
          return MakeUnexpected(n.error());
        }
        next = n.value();
        LinkChild(cur, next);
      }
      cur = next;
    }
    return cur;
  }

  template <typename F>
  Expected<void, CommonError> Visit(StringView prefix, F &fn) const {
    Index start = Walk(prefix);
    if (start == None())
      return {};
    Vector<char> key(m_nodes.get_allocator());
    Expected<void, CommonError> r = key.reserve(prefix.size() + 16);
    if (!r)
      return r;
    for (size_t i = 0; i < prefix.size(); ++i)
      (void)key.push_back(prefix[i]); // reserved above
    Index cur = start;
    for (;;) {
      const Node &n = m_nodes[cur];
      if (n.value.has_value())
        fn(StringView(key.data(), key.size()), n.value.value());
      if (n.first_child != None()) {
        cur = n.first_child;
        r = key.push_back(static_cast<char>(m_nodes[cur].ch));
        if (!r)
          return r;
        continue;
      }
      for (;;) {
        if (cur == start)
          return {};
        if (m_nodes[cur].next_sibling != None()) {
          cur = m_nodes[cur].next_sibling;
          key.back() = static_cast<char>(m_nodes[cur].ch);
          break;
        }
        cur = m_nodes[cur].parent;
        key.pop_back();
      }
    }
  }

public:
  typedef Expected<Pair<V *, bool>, CommonError> InsertResult;

  struct PrefixMatch {
    size_t length; // number of bytes of the text that the key covers
    V *value;      // nullptr when no key is a prefix of the text
  };
  struct ConstPrefixMatch {
    size_t length;
    const V *value;
  };

  explicit Trie(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_nodes(alloc), m_free(None()), m_size(0) {}
  Trie(Trie &&o) noexcept
      : m_nodes(Move(o.m_nodes)), m_free(o.m_free), m_size(o.m_size) {
    o.m_free = None();
    o.m_size = 0;
  }
  Trie &operator=(Trie &&o) noexcept {
    if (this != &o) {
      m_nodes = Move(o.m_nodes);
      m_free = o.m_free;
      m_size = o.m_size;
      o.m_free = None();
      o.m_size = 0;
    }
    return *this;
  }
  Trie(const Trie &) = delete;
  Trie &operator=(const Trie &) = delete;

  VAPOR_NODISCARD size_t size() const noexcept { return m_size; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }
  VAPOR_NODISCARD Allocator &get_allocator() const noexcept {
    return m_nodes.get_allocator();
  }

  void clear() noexcept {
    m_nodes.clear();
    m_free = None();
    m_size = 0;
  }

  VAPOR_NODISCARD V *find(StringView key) noexcept {
    Index n = Walk(key);
    return n != None() && m_nodes[n].value.has_value()
               ? &m_nodes[n].value.value()
               : nullptr;
  }
  VAPOR_NODISCARD const V *find(StringView key) const noexcept {
    Index n = Walk(key);
    return n != None() && m_nodes[n].value.has_value()
               ? &m_nodes[n].value.value()
               : nullptr;
  }
  VAPOR_NODISCARD bool contains(StringView key) const noexcept {
    return find(key) != nullptr;
  }

  // Stores V(args...) under `key` only if the key has no value yet.
  template <typename... Args>
  VAPOR_NODISCARD InsertResult try_emplace(StringView key, Args &&...args) {
    Expected<Index, CommonError> n = Ensure(key);
    if (!n)
      return MakeUnexpected(n.error());
    Node &node = m_nodes[n.value()];
    if (node.value.has_value())
      return Pair<V *, bool>(&node.value.value(), false);
    node.value.emplace(Forward<Args>(args)...);
    ++m_size;
    return Pair<V *, bool>(&node.value.value(), true);
  }

  template <typename M>
  VAPOR_NODISCARD InsertResult insert_or_assign(StringView key, M &&value) {
    Expected<Index, CommonError> n = Ensure(key);
    if (!n)
      return MakeUnexpected(n.error());
    Node &node = m_nodes[n.value()];
    if (node.value.has_value()) {
      node.value.value() = Forward<M>(value);
      return Pair<V *, bool>(&node.value.value(), false);
    }
    node.value.emplace(Forward<M>(value));
    ++m_size;
    return Pair<V *, bool>(&node.value.value(), true);
  }

  // Removes the key; returns whether it was present.
  bool erase(StringView key) noexcept {
    Index n = Walk(key);
    if (n == None() || !m_nodes[n].value.has_value())
      return false;
    m_nodes[n].value.reset();
    --m_size;
    Prune(n);
    return true;
  }

  // The longest stored key that is a prefix of `text` (possibly the empty
  // key). `value` is nullptr when none matches.
  VAPOR_NODISCARD PrefixMatch LongestPrefix(StringView text) noexcept {
    ConstPrefixMatch m = static_cast<const Trie *>(this)->LongestPrefix(text);
    PrefixMatch out = {m.length, const_cast<V *>(m.value)};
    return out;
  }
  VAPOR_NODISCARD ConstPrefixMatch
  LongestPrefix(StringView text) const noexcept {
    ConstPrefixMatch best = {0, nullptr};
    if (m_nodes.empty())
      return best;
    Index cur = 0;
    size_t i = 0;
    for (;;) {
      if (m_nodes[cur].value.has_value()) {
        best.length = i;
        best.value = &m_nodes[cur].value.value();
      }
      if (i == text.size())
        break;
      cur = FindChild(cur, Byte(text[i]));
      if (cur == None())
        break;
      ++i;
    }
    return best;
  }

  // True when some stored key starts with `prefix`.
  VAPOR_NODISCARD bool HasPrefix(StringView prefix) const noexcept {
    return Walk(prefix) != None() && m_size != 0;
  }

  // Calls fn(StringView key, V &value) for every key starting with `prefix`,
  // in ascending byte order. The key view is only valid during the call.
  // Fails only when the temporary key buffer cannot be allocated.
  template <typename F>
  Expected<void, CommonError> ForEach(StringView prefix, F fn) {
    struct Adapter {
      F &f;
      void operator()(StringView k, const V &v) const {
        f(k, const_cast<V &>(v));
      }
    };
    Adapter a = {fn};
    return Visit(prefix, a);
  }
  template <typename F>
  Expected<void, CommonError> ForEach(StringView prefix, F fn) const {
    return Visit(prefix, fn);
  }
};

} // namespace vapor
