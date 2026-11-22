#pragma once
#include <assert.h>
#include <stdlib.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/type_traits.hpp>

// List<T>: a doubly linked list modelled on std::list.
//
// Why: a Vector keeps elements side by side, which is fastest to read but
// makes inserting or removing in the middle O(n) and moves elements around
// (invalidating pointers to them). A List allocates one node per element, so
// inserting or erasing anywhere is O(1) once you hold an iterator, and
// pointers and iterators to other elements stay valid forever. Good for
// work queues you reorder, LRU caches (move an entry to the front with
// splice, no allocation), and objects that must not move in memory.
//
// How:
//   List<int> l;
//   (void)l.push_back(2);                  // Expected<void, CommonError>
//   auto it = l.insert(l.begin(), 1);      // Expected<iterator, CommonError>
//   l.erase(it.value());                   // returns the next iterator
//   l.splice(l.end(), other);              // moves nodes, cannot fail
//   l.sort();  l.reverse();  l.unique();  l.remove(3);
//   for (int x : l) ...
//
// Differences from std::list forced by the lack of exceptions: anything that
// allocates (push, insert, emplace) returns Expected and leaves the list
// unchanged on failure; copying is the explicit fallible Clone(); there is no
// element-count constructor. splice and merge move nodes between lists
// without allocating, so both lists MUST use the same allocator. size() is
// O(1). Not thread-safe.

namespace vapor {

namespace detail {
struct ListNodeBase {
  ListNodeBase *prev;
  ListNodeBase *next;
};
} // namespace detail

template <typename T> class List {
  typedef detail::ListNodeBase NodeBase;

  struct Node : NodeBase {
    T value;
    template <typename... Args>
    explicit Node(Args &&...args) : NodeBase(), value(Forward<Args>(args)...) {}
  };

public:
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T &reference;
  typedef const T &const_reference;

  template <typename V> class Iter {
    friend class List;
    template <typename W> friend class Iter;
    NodeBase *m_node;
    explicit Iter(NodeBase *n) noexcept : m_node(n) {}

  public:
    typedef V value_type;
    typedef ptrdiff_t difference_type;
    typedef V *pointer;
    typedef V &reference;

    Iter() noexcept : m_node(nullptr) {}
    template <typename W, typename = EnableIfT<IsSame<const W, V>::value &&
                                               !IsSame<W, V>::value>>
    Iter(const Iter<W> &o) noexcept : m_node(o.m_node) {}

    V &operator*() const noexcept { return static_cast<Node *>(m_node)->value; }
    V *operator->() const noexcept {
      return &static_cast<Node *>(m_node)->value;
    }
    Iter &operator++() noexcept {
      m_node = m_node->next;
      return *this;
    }
    Iter operator++(int) noexcept {
      Iter t(*this);
      m_node = m_node->next;
      return t;
    }
    Iter &operator--() noexcept {
      m_node = m_node->prev;
      return *this;
    }
    Iter operator--(int) noexcept {
      Iter t(*this);
      m_node = m_node->prev;
      return t;
    }
    friend bool operator==(const Iter &a, const Iter &b) noexcept {
      return a.m_node == b.m_node;
    }
    friend bool operator!=(const Iter &a, const Iter &b) noexcept {
      return a.m_node != b.m_node;
    }
  };
  typedef Iter<T> iterator;
  typedef Iter<const T> const_iterator;

private:
  Allocator *m_alloc;
  NodeBase m_end; // sentinel: next = first element, prev = last element
  size_type m_size;

  void InitEmpty() noexcept {
    m_end.prev = m_end.next = &m_end;
    m_size = 0;
  }

  static void LinkBefore(NodeBase *pos, NodeBase *n) noexcept {
    n->prev = pos->prev;
    n->next = pos;
    pos->prev->next = n;
    pos->prev = n;
  }
  static void Unlink(NodeBase *n) noexcept {
    n->prev->next = n->next;
    n->next->prev = n->prev;
  }

  void FreeNode(NodeBase *n) noexcept {
    Node *node = static_cast<Node *>(n);
    detail::DestroyAt(node);
    m_alloc->Deallocate(node, sizeof(Node));
  }

  template <typename... Args>
  Expected<iterator, CommonError> EmplaceBefore(NodeBase *pos, Args &&...args) {
    void *mem = m_alloc->Allocate(sizeof(Node), alignof(Node));
    if (!mem)
      return MakeUnexpected(CommonError::OutOfMemory);
    Node *n = detail::ConstructAt<Node>(mem, Forward<Args>(args)...);
    LinkBefore(pos, n);
    ++m_size;
    return iterator(n);
  }

  // Singly linked helpers used by sort/merge: chains end with nullptr.
  template <typename Cmp>
  static NodeBase *MergeChains(NodeBase *a, NodeBase *b, Cmp &cmp) noexcept {
    NodeBase head;
    NodeBase *tail = &head;
    while (a && b) {
      // Take from `b` only when strictly smaller, which keeps it stable.
      if (cmp(static_cast<Node *>(b)->value, static_cast<Node *>(a)->value)) {
        tail->next = b;
        b = b->next;
      } else {
        tail->next = a;
        a = a->next;
      }
      tail = tail->next;
    }
    tail->next = a ? a : b;
    return head.next;
  }

  // Sorts the first n nodes of the chain starting at `head`; `rest` receives
  // the node after them.
  template <typename Cmp>
  static NodeBase *SortChain(NodeBase *head, size_type n, NodeBase *&rest,
                             Cmp &cmp) noexcept {
    if (n == 1) {
      rest = head->next;
      head->next = nullptr;
      return head;
    }
    size_type half = n / 2;
    NodeBase *mid;
    NodeBase *a = SortChain(head, half, mid, cmp);
    NodeBase *b = SortChain(mid, n - half, rest, cmp);
    return MergeChains(a, b, cmp);
  }

  // Detaches all nodes into a chain that ends with nullptr.
  NodeBase *TakeChain() noexcept {
    NodeBase *first = m_end.next;
    m_end.prev->next = nullptr;
    InitEmpty();
    return first == &m_end ? nullptr : first;
  }

  // Adopts a chain (ending with nullptr) of `count` nodes into this empty list.
  void AdoptChain(NodeBase *first, size_type count) noexcept {
    NodeBase *prev = &m_end;
    for (NodeBase *n = first; n; n = n->next) {
      n->prev = prev;
      prev->next = n;
      prev = n;
    }
    prev->next = &m_end;
    m_end.prev = prev;
    m_size = count;
  }

public:
  explicit List(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_alloc(&alloc) {
    InitEmpty();
  }
  List(List &&o) noexcept : m_alloc(o.m_alloc) {
    InitEmpty();
    TakeAll(o);
  }
  List &operator=(List &&o) noexcept {
    if (this != &o) {
      clear();
      m_alloc = o.m_alloc;
      TakeAll(o);
    }
    return *this;
  }
  List(const List &) = delete;
  List &operator=(const List &) = delete;
  ~List() { clear(); }

  VAPOR_NODISCARD Expected<List, CommonError> Clone() const {
    List copy(*m_alloc);
    for (const_iterator it = begin(); it != end(); ++it) {
      Expected<void, CommonError> r = copy.push_back(*it);
      if (!r)
        return MakeUnexpected(r.error());
    }
    return copy;
  }

  VAPOR_NODISCARD Allocator &get_allocator() const noexcept { return *m_alloc; }
  VAPOR_NODISCARD size_type size() const noexcept { return m_size; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_size == 0; }
  VAPOR_NODISCARD size_type max_size() const noexcept {
    return VAPOR_SIZE_MAX / sizeof(Node);
  }

  VAPOR_NODISCARD iterator begin() noexcept { return iterator(m_end.next); }
  VAPOR_NODISCARD iterator end() noexcept { return iterator(&m_end); }
  VAPOR_NODISCARD const_iterator begin() const noexcept {
    return const_iterator(const_cast<NodeBase *>(m_end.next));
  }
  VAPOR_NODISCARD const_iterator end() const noexcept {
    return const_iterator(const_cast<NodeBase *>(&m_end));
  }
  VAPOR_NODISCARD const_iterator cbegin() const noexcept { return begin(); }
  VAPOR_NODISCARD const_iterator cend() const noexcept { return end(); }

  VAPOR_NODISCARD reference front() noexcept {
    assert(m_size);
    return *begin();
  }
  VAPOR_NODISCARD const_reference front() const noexcept {
    assert(m_size);
    return *begin();
  }
  VAPOR_NODISCARD reference back() noexcept {
    assert(m_size);
    return static_cast<Node *>(m_end.prev)->value;
  }
  VAPOR_NODISCARD const_reference back() const noexcept {
    assert(m_size);
    return static_cast<const Node *>(m_end.prev)->value;
  }

  template <typename... Args>
  Expected<iterator, CommonError> emplace(const_iterator pos, Args &&...args) {
    return EmplaceBefore(pos.m_node, Forward<Args>(args)...);
  }
  Expected<iterator, CommonError> insert(const_iterator pos, const T &v) {
    return EmplaceBefore(pos.m_node, v);
  }
  Expected<iterator, CommonError> insert(const_iterator pos, T &&v) {
    return EmplaceBefore(pos.m_node, Move(v));
  }
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_back(Args &&...args) {
    Expected<iterator, CommonError> r =
        EmplaceBefore(&m_end, Forward<Args>(args)...);
    if (!r)
      return MakeUnexpected(r.error());
    return ReferenceWrapper<T>(*r.value());
  }
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace_front(Args &&...args) {
    Expected<iterator, CommonError> r =
        EmplaceBefore(m_end.next, Forward<Args>(args)...);
    if (!r)
      return MakeUnexpected(r.error());
    return ReferenceWrapper<T>(*r.value());
  }
  Expected<void, CommonError> push_back(const T &v) {
    Expected<iterator, CommonError> r = EmplaceBefore(&m_end, v);
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }
  Expected<void, CommonError> push_back(T &&v) {
    Expected<iterator, CommonError> r = EmplaceBefore(&m_end, Move(v));
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }
  Expected<void, CommonError> push_front(const T &v) {
    Expected<iterator, CommonError> r = EmplaceBefore(m_end.next, v);
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }
  Expected<void, CommonError> push_front(T &&v) {
    Expected<iterator, CommonError> r = EmplaceBefore(m_end.next, Move(v));
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }

  // Removes the element at `pos` (not end()); returns the following element.
  iterator erase(const_iterator pos) noexcept {
    assert(pos.m_node != &m_end);
    NodeBase *next = pos.m_node->next;
    Unlink(pos.m_node);
    FreeNode(pos.m_node);
    --m_size;
    return iterator(next);
  }
  iterator erase(const_iterator first, const_iterator last) noexcept {
    while (first != last)
      first = erase(first);
    return iterator(last.m_node);
  }
  void pop_front() noexcept {
    assert(m_size);
    erase(begin());
  }
  void pop_back() noexcept {
    assert(m_size);
    erase(const_iterator(m_end.prev));
  }
  void clear() noexcept {
    NodeBase *n = m_end.next;
    while (n != &m_end) {
      NodeBase *next = n->next;
      FreeNode(n);
      n = next;
    }
    InitEmpty();
  }

  void swap(List &o) noexcept {
    List tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }

  // --- operations that only relink nodes (cannot fail)

  // Moves all of `other` before `pos`. Both lists must share an allocator.
  void splice(const_iterator pos, List &other) noexcept {
    if (other.empty() || &other == this)
      return;
    assert(m_alloc == other.m_alloc);
    NodeBase *first = other.m_end.next, *last = other.m_end.prev;
    SpliceNodes(pos.m_node, first, last);
    m_size += other.m_size;
    other.InitEmpty();
  }
  // Moves the single element `it` of `other` before `pos`.
  void splice(const_iterator pos, List &other, const_iterator it) noexcept {
    assert(m_alloc == other.m_alloc);
    if (it.m_node == pos.m_node || it.m_node->next == pos.m_node)
      return;
    Unlink(it.m_node);
    LinkBefore(pos.m_node, it.m_node);
    if (&other != this) {
      --other.m_size;
      ++m_size;
    }
  }
  // Moves [first, last) of `other` before `pos` (pos must not be inside it).
  void splice(const_iterator pos, List &other, const_iterator first,
              const_iterator last) noexcept {
    assert(m_alloc == other.m_alloc);
    if (first == last)
      return;
    size_type n = 0;
    if (&other != this)
      for (const_iterator i = first; i != last; ++i)
        ++n;
    NodeBase *f = first.m_node;
    NodeBase *l = last.m_node->prev;
    SpliceNodes(pos.m_node, f, l);
    if (&other != this) {
      other.m_size -= n;
      m_size += n;
    }
  }

  size_type remove(const T &value) noexcept {
    size_type removed = 0;
    for (iterator it = begin(); it != end();) {
      if (*it == value) {
        it = erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }
  template <typename Pred> size_type remove_if(Pred pred) noexcept {
    size_type removed = 0;
    for (iterator it = begin(); it != end();) {
      if (pred(*it)) {
        it = erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }

  void reverse() noexcept {
    NodeBase *n = &m_end;
    do {
      NodeBase *next = n->next;
      n->next = n->prev;
      n->prev = next;
      n = next;
    } while (n != &m_end);
  }

  // Removes consecutive equal elements.
  size_type unique() noexcept {
    size_type removed = 0;
    iterator it = begin();
    while (it != end()) {
      iterator next = it;
      ++next;
      if (next != end() && *it == *next) {
        erase(next);
        ++removed;
      } else {
        it = next;
      }
    }
    return removed;
  }

  // Stable sort, O(n log n), no allocation.
  void sort() noexcept { sort(Less<T>()); }
  template <typename Cmp> void sort(Cmp cmp) noexcept {
    if (m_size < 2)
      return;
    size_type n = m_size;
    NodeBase *chain = TakeChain();
    NodeBase *rest = nullptr;
    NodeBase *sorted = SortChain(chain, n, rest, cmp);
    AdoptChain(sorted, n);
  }

  // Merges the sorted list `other` into this sorted list (stable; `other`
  // becomes empty). Same allocator required.
  void merge(List &other) noexcept { merge(other, Less<T>()); }
  template <typename Cmp> void merge(List &other, Cmp cmp) noexcept {
    if (&other == this || other.empty())
      return;
    assert(m_alloc == other.m_alloc);
    size_type n = m_size + other.m_size;
    NodeBase *a = TakeChain();
    NodeBase *b = other.TakeChain();
    AdoptChain(MergeChains(a, b, cmp), n);
  }

private:
  void SpliceNodes(NodeBase *pos, NodeBase *first, NodeBase *last) noexcept {
    // Detach [first, last] from wherever it is, then link it before pos.
    first->prev->next = last->next;
    last->next->prev = first->prev;
    first->prev = pos->prev;
    last->next = pos;
    pos->prev->next = first;
    pos->prev = last;
  }

  void TakeAll(List &o) noexcept {
    if (o.m_size == 0)
      return;
    m_end.next = o.m_end.next;
    m_end.prev = o.m_end.prev;
    m_end.next->prev = &m_end;
    m_end.prev->next = &m_end;
    m_size = o.m_size;
    o.InitEmpty();
  }
};

} // namespace vapor
