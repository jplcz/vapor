#pragma once
#include <assert.h>
#include <stddef.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>
#include <vapor/type_traits.hpp>

// Engine shared by Map and Set: a height-balanced (AVL) binary search tree
// of individually allocated nodes. Not part of the public API.
//
// AVL rule: for every node, the heights of its two subtrees differ by at most
// one, so lookup, insert and erase are O(log n). Elements never move once
// inserted (nodes are only relinked), so references to elements stay valid
// until that element is erased.

namespace vapor {
namespace detail {

struct AvlNodeBase {
  AvlNodeBase *left;
  AvlNodeBase *right;
  AvlNodeBase *parent;
  int height;
};

template <typename Value, typename KeyOf, typename Cmp> class AvlTree {
public:
  typedef typename KeyOf::key_type key_type;
  typedef AvlNodeBase NodeBase;

private:
  struct Node : NodeBase {
    alignas(Value) unsigned char raw[sizeof(Value)];
    Value *Get() noexcept { return reinterpret_cast<Value *>(raw); }
  };

  Allocator *m_alloc;
  NodeBase *m_root;
  size_t m_size;
  Cmp m_cmp;
  KeyOf m_key;

  static int H(const NodeBase *n) noexcept { return n ? n->height : 0; }
  static void Update(NodeBase *n) noexcept {
    int l = H(n->left), r = H(n->right);
    n->height = 1 + (l > r ? l : r);
  }
  static int Balance(const NodeBase *n) noexcept {
    return H(n->left) - H(n->right);
  }

  static NodeBase *Min(NodeBase *n) noexcept {
    while (n->left)
      n = n->left;
    return n;
  }
  static NodeBase *Max(NodeBase *n) noexcept {
    while (n->right)
      n = n->right;
    return n;
  }

  // Makes `v` take the place of `u` under u's parent (v may be null).
  void Replace(NodeBase *u, NodeBase *v) noexcept {
    if (!u->parent)
      m_root = v;
    else if (u == u->parent->left)
      u->parent->left = v;
    else
      u->parent->right = v;
    if (v)
      v->parent = u->parent;
  }

  NodeBase *RotateLeft(NodeBase *x) noexcept {
    NodeBase *y = x->right;
    x->right = y->left;
    if (y->left)
      y->left->parent = x;
    Replace(x, y);
    y->left = x;
    x->parent = y;
    Update(x);
    Update(y);
    return y;
  }
  NodeBase *RotateRight(NodeBase *x) noexcept {
    NodeBase *y = x->left;
    x->left = y->right;
    if (y->right)
      y->right->parent = x;
    Replace(x, y);
    y->right = x;
    x->parent = y;
    Update(x);
    Update(y);
    return y;
  }

  // Restores the AVL rule on the path from `n` up to the root.
  void RebalanceFrom(NodeBase *n) noexcept {
    while (n) {
      Update(n);
      int b = Balance(n);
      NodeBase *top = n;
      if (b > 1) {
        if (Balance(n->left) < 0)
          RotateLeft(n->left);
        top = RotateRight(n);
      } else if (b < -1) {
        if (Balance(n->right) > 0)
          RotateRight(n->right);
        top = RotateLeft(n);
      }
      n = top->parent;
    }
  }

  void FreeNode(NodeBase *n) noexcept {
    Node *node = static_cast<Node *>(n);
    DestroyAt(node->Get());
    m_alloc->Deallocate(node, sizeof(Node));
  }

  template <typename Maker>
  Expected<NodeBase *, CommonError> LinkNew(NodeBase *parent, bool go_left,
                                            Maker &make) {
    void *mem = m_alloc->Allocate(sizeof(Node), alignof(Node));
    if (!mem)
      return MakeUnexpected(CommonError::OutOfMemory);
    Node *node = static_cast<Node *>(mem);
    node->left = node->right = nullptr;
    node->parent = parent;
    node->height = 1;
    make(static_cast<void *>(node->raw));
    if (!parent)
      m_root = node;
    else if (go_left)
      parent->left = node;
    else
      parent->right = node;
    ++m_size;
    RebalanceFrom(parent);
    return static_cast<NodeBase *>(node);
  }

  void Reset() noexcept {
    m_root = nullptr;
    m_size = 0;
  }

public:
  template <typename V> class Iter {
    friend class AvlTree;
    template <typename W> friend class Iter;
    const AvlTree *m_tree;
    NodeBase *m_node; // nullptr == end()
    Iter(const AvlTree *t, NodeBase *n) noexcept : m_tree(t), m_node(n) {}

  public:
    typedef V value_type;
    typedef ptrdiff_t difference_type;
    typedef V *pointer;
    typedef V &reference;

    Iter() noexcept : m_tree(nullptr), m_node(nullptr) {}
    template <typename W, typename = EnableIfT<IsSame<const W, V>::value &&
                                               !IsSame<W, V>::value>>
    Iter(const Iter<W> &o) noexcept : m_tree(o.m_tree), m_node(o.m_node) {}

    V &operator*() const noexcept {
      return *static_cast<Node *>(m_node)->Get();
    }
    V *operator->() const noexcept {
      return static_cast<Node *>(m_node)->Get();
    }
    Iter &operator++() noexcept {
      m_node = Successor(m_node);
      return *this;
    }
    Iter operator++(int) noexcept {
      Iter t(*this);
      ++*this;
      return t;
    }
    Iter &operator--() noexcept {
      m_node = m_node ? Predecessor(m_node) : Max(m_tree->m_root);
      return *this;
    }
    Iter operator--(int) noexcept {
      Iter t(*this);
      --*this;
      return t;
    }
    friend bool operator==(const Iter &a, const Iter &b) noexcept {
      return a.m_node == b.m_node;
    }
    friend bool operator!=(const Iter &a, const Iter &b) noexcept {
      return a.m_node != b.m_node;
    }
  };
  typedef Iter<Value> iterator;
  typedef Iter<const Value> const_iterator;

  static NodeBase *Successor(NodeBase *n) noexcept {
    if (n->right)
      return Min(n->right);
    while (n->parent && n == n->parent->right)
      n = n->parent;
    return n->parent;
  }
  static NodeBase *Predecessor(NodeBase *n) noexcept {
    if (n->left)
      return Max(n->left);
    while (n->parent && n == n->parent->left)
      n = n->parent;
    return n->parent;
  }

  AvlTree(Allocator &alloc, const Cmp &cmp) noexcept
      : m_alloc(&alloc), m_root(nullptr), m_size(0), m_cmp(cmp), m_key() {}
  AvlTree(AvlTree &&o) noexcept
      : m_alloc(o.m_alloc), m_root(o.m_root), m_size(o.m_size), m_cmp(o.m_cmp),
        m_key() {
    o.Reset();
  }
  AvlTree &operator=(AvlTree &&o) noexcept {
    if (this != &o) {
      clear();
      m_alloc = o.m_alloc;
      m_root = o.m_root;
      m_size = o.m_size;
      m_cmp = o.m_cmp;
      o.Reset();
    }
    return *this;
  }
  AvlTree(const AvlTree &) = delete;
  AvlTree &operator=(const AvlTree &) = delete;
  ~AvlTree() { clear(); }

  Allocator &get_allocator() const noexcept { return *m_alloc; }
  const Cmp &key_comp() const noexcept { return m_cmp; }
  size_t size() const noexcept { return m_size; }
  bool empty() const noexcept { return m_size == 0; }

  iterator begin() noexcept {
    return iterator(this, m_root ? Min(m_root) : nullptr);
  }
  iterator end() noexcept { return iterator(this, nullptr); }
  const_iterator begin() const noexcept {
    return const_iterator(this, m_root ? Min(m_root) : nullptr);
  }
  const_iterator end() const noexcept { return const_iterator(this, nullptr); }
  iterator MakeIterator(NodeBase *n) noexcept { return iterator(this, n); }
  const_iterator MakeIterator(NodeBase *n) const noexcept {
    return const_iterator(this, n);
  }
  static NodeBase *NodeOf(const_iterator it) noexcept { return it.m_node; }

  // First node whose key is not less than `key`, or nullptr.
  NodeBase *LowerBound(const key_type &key) const noexcept {
    NodeBase *n = m_root, *best = nullptr;
    while (n) {
      if (m_cmp(m_key(*static_cast<Node *>(n)->Get()), key)) {
        n = n->right;
      } else {
        best = n;
        n = n->left;
      }
    }
    return best;
  }
  // First node whose key is greater than `key`, or nullptr.
  NodeBase *UpperBound(const key_type &key) const noexcept {
    NodeBase *n = m_root, *best = nullptr;
    while (n) {
      if (m_cmp(key, m_key(*static_cast<Node *>(n)->Get()))) {
        best = n;
        n = n->left;
      } else {
        n = n->right;
      }
    }
    return best;
  }
  NodeBase *Find(const key_type &key) const noexcept {
    NodeBase *n = LowerBound(key);
    if (n && !m_cmp(key, m_key(*static_cast<Node *>(n)->Get())))
      return n;
    return nullptr;
  }

  // Inserts a new element unless an equivalent key exists. `make(void *mem)`
  // must construct the Value in `mem`. Returns {node, inserted}.
  template <typename Maker>
  Expected<Pair<NodeBase *, bool>, CommonError> InsertWith(const key_type &key,
                                                           Maker make) {
    NodeBase *parent = nullptr;
    NodeBase *n = m_root;
    bool go_left = false;
    while (n) {
      parent = n;
      const key_type &nk = m_key(*static_cast<Node *>(n)->Get());
      if (m_cmp(key, nk)) {
        go_left = true;
        n = n->left;
      } else if (m_cmp(nk, key)) {
        go_left = false;
        n = n->right;
      } else {
        return Pair<NodeBase *, bool>(n, false);
      }
    }
    Expected<NodeBase *, CommonError> r = LinkNew(parent, go_left, make);
    if (!r)
      return MakeUnexpected(r.error());
    return Pair<NodeBase *, bool>(r.value(), true);
  }

  // Like InsertWith but always inserts; an element equivalent to an existing
  // one goes after all of them, so equal keys keep insertion order.
  template <typename Maker>
  Expected<NodeBase *, CommonError> InsertEqual(const key_type &key,
                                                Maker make) {
    NodeBase *parent = nullptr;
    NodeBase *n = m_root;
    bool go_left = false;
    while (n) {
      parent = n;
      go_left = m_cmp(key, m_key(*static_cast<Node *>(n)->Get()));
      n = go_left ? n->left : n->right;
    }
    return LinkNew(parent, go_left, make);
  }

  void EraseNode(NodeBase *z) noexcept {
    NodeBase *rebalance_from;
    if (!z->left || !z->right) {
      rebalance_from = z->parent;
      Replace(z, z->left ? z->left : z->right);
    } else {
      NodeBase *y = Min(z->right); // in-order successor takes z's place
      if (y->parent == z) {
        rebalance_from = y;
      } else {
        rebalance_from = y->parent;
        Replace(y, y->right);
        y->right = z->right;
        y->right->parent = y;
      }
      Replace(z, y);
      y->left = z->left;
      y->left->parent = y;
      y->height = z->height;
    }
    --m_size;
    RebalanceFrom(rebalance_from);
    FreeNode(z);
  }

  void clear() noexcept {
    NodeBase *n = m_root;
    while (n) {
      if (n->left) {
        n = n->left;
      } else if (n->right) {
        n = n->right;
      } else {
        NodeBase *p = n->parent;
        if (p) {
          if (p->left == n)
            p->left = nullptr;
          else
            p->right = nullptr;
        }
        FreeNode(n);
        n = p;
      }
    }
    Reset();
  }

  void swap(AvlTree &o) noexcept {
    NodeBase *r = m_root;
    size_t s = m_size;
    Allocator *a = m_alloc;
    Cmp c = m_cmp;
    m_root = o.m_root;
    m_size = o.m_size;
    m_alloc = o.m_alloc;
    m_cmp = o.m_cmp;
    o.m_root = r;
    o.m_size = s;
    o.m_alloc = a;
    o.m_cmp = c;
  }

  // Test hook: verifies ordering links, heights and balance.
  bool CheckInvariants() const noexcept {
    return CheckNode(m_root, nullptr) >= 0;
  }

private:
  int CheckNode(const NodeBase *n, const NodeBase *parent) const noexcept {
    if (!n)
      return 0;
    if (n->parent != parent)
      return -1;
    int l = CheckNode(n->left, n), r = CheckNode(n->right, n);
    if (l < 0 || r < 0 || l - r > 1 || r - l > 1)
      return -1;
    int h = 1 + (l > r ? l : r);
    return h == n->height ? h : -1;
  }
};

} // namespace detail
} // namespace vapor
