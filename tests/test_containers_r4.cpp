#include <vapor/deque.hpp>
#include <vapor/list.hpp>
#include <vapor/map.hpp>
#include <vapor/ring_buffer.hpp>
#include <vapor/set.hpp>
#include <vapor/small_vector.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using namespace vapor;
using vapor_test::TestAllocator;
typedef vapor_test::TrackedT<61> Tr;

TEST(SmallVector, SpillsToHeapAndBack) {
  TestAllocator alloc;
  {
    SmallVector<int, 4> v(alloc);
    for (int i = 0; i < 4; ++i)
      ASSERT_TRUE(v.push_back(i).has_value());
    EXPECT_TRUE(v.is_inline());
    EXPECT_EQ(alloc.live, 0);
    ASSERT_TRUE(v.push_back(4).has_value());
    EXPECT_FALSE(v.is_inline());
    EXPECT_EQ(alloc.live, 1);
    for (int i = 0; i < 5; ++i)
      EXPECT_EQ(v[static_cast<size_t>(i)], i);
    v.erase(v.begin());
    v.erase(v.begin());
    ASSERT_TRUE(v.shrink_to_fit().has_value());
    EXPECT_TRUE(v.is_inline());
    EXPECT_EQ(alloc.live, 0);
    EXPECT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0], 2);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(SmallVector, MoveCloneAndOom) {
  TestAllocator alloc;
  SmallVector<int, 2> a(alloc);
  for (int i = 0; i < 6; ++i)
    ASSERT_TRUE(a.push_back(i).has_value());
  SmallVector<int, 2> b(Move(a));
  EXPECT_EQ(b.size(), 6u);
  EXPECT_TRUE(a.empty());
  auto c = b.Clone();
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c.value().size(), 6u);
  EXPECT_EQ(c.value()[5], 5);
  EXPECT_EQ(b.checked_at(9).error(), CommonError::OutOfBounds);

  SmallVector<int, 2> d(alloc);
  ASSERT_TRUE(d.push_back(1).has_value());
  ASSERT_TRUE(d.push_back(2).has_value());
  alloc.fail = true;
  EXPECT_EQ(d.push_back(3).error(), CommonError::OutOfMemory);
  EXPECT_EQ(d.size(), 2u);
  alloc.fail = false;
}

TEST(SmallVector, DestroysElements) {
  Tr::alive = 0;
  TestAllocator alloc;
  {
    SmallVector<Tr, 2> v(alloc);
    for (int i = 0; i < 5; ++i)
      ASSERT_TRUE(v.emplace_back(i).has_value());
    EXPECT_EQ(Tr::alive, 5);
    ASSERT_TRUE(v.resize(1).has_value());
    EXPECT_EQ(Tr::alive, 1);
  }
  EXPECT_EQ(Tr::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(Deque, BothEndsAndWrap) {
  TestAllocator alloc;
  {
    Deque<int> d(alloc);
    for (int i = 0; i < 100; ++i) {
      ASSERT_TRUE(d.push_back(i).has_value());
      ASSERT_TRUE(d.push_front(-i - 1).has_value());
    }
    EXPECT_EQ(d.size(), 200u);
    EXPECT_EQ(d.front(), -100);
    EXPECT_EQ(d.back(), 99);
    EXPECT_EQ(d[100], 0);
    for (int i = 0; i < 50; ++i) {
      d.pop_front();
      d.pop_back();
    }
    EXPECT_EQ(d.size(), 100u);
    int expect = -50;
    for (int x : d)
      EXPECT_EQ(x, expect++);
    auto c = d.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().size(), 100u);
    EXPECT_EQ(c.value()[0], -50);
    EXPECT_EQ(d.checked_at(500).error(), CommonError::OutOfBounds);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(Deque, OomAndLeaks) {
  Tr::alive = 0;
  TestAllocator alloc;
  {
    Deque<Tr> d(alloc);
    for (int i = 0; i < 20; ++i)
      ASSERT_TRUE(d.emplace_front(i).has_value());
    alloc.fail = true;
    size_t before = d.size();
    for (int i = 0; i < 100 && d.push_back(Tr(1)).has_value(); ++i) {
    }
    EXPECT_GE(d.size(), before);
    alloc.fail = false;
  }
  EXPECT_EQ(Tr::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(RingBuffer, FullOverwriteAndOrder) {
  RingBuffer<int, 3> rb;
  for (int i = 0; i < 3; ++i)
    ASSERT_TRUE(rb.push_back(i).has_value());
  EXPECT_TRUE(rb.full());
  EXPECT_EQ(rb.push_back(9).error(), CommonError::CapacityExceeded);
  rb.PushOverwrite(3);
  EXPECT_EQ(rb.front(), 1);
  EXPECT_EQ(rb.back(), 3);
  int expect = 1;
  for (int x : rb)
    EXPECT_EQ(x, expect++);
  rb.pop_front();
  rb.pop_back();
  EXPECT_EQ(rb.size(), 1u);
  EXPECT_EQ(rb[0], 2);
  RingBuffer<int, 3> copy(rb);
  EXPECT_EQ(copy.size(), 1u);
}

TEST(RingBuffer, DestroysElements) {
  Tr::alive = 0;
  {
    RingBuffer<Tr, 4> rb;
    for (int i = 0; i < 10; ++i)
      rb.PushOverwrite(Tr(i));
    EXPECT_EQ(Tr::alive, 4);
    EXPECT_EQ(rb.front().value, 6);
  }
  EXPECT_EQ(Tr::alive, 0);
}

static void Fill(List<int> &l, const int *v, size_t n) {
  for (size_t i = 0; i < n; ++i)
    ASSERT_TRUE(l.push_back(v[i]).has_value());
}
static bool Eq(const List<int> &l, const int *v, size_t n) {
  if (l.size() != n)
    return false;
  size_t i = 0;
  for (int x : l)
    if (x != v[i++])
      return false;
  return true;
}

TEST(List, PushPopAndIterate) {
  TestAllocator alloc;
  {
    List<int> l(alloc);
    const int in[] = {1, 2, 3};
    Fill(l, in, 3);
    ASSERT_TRUE(l.push_front(0).has_value());
    const int out[] = {0, 1, 2, 3};
    EXPECT_TRUE(Eq(l, out, 4));
    l.pop_front();
    l.pop_back();
    const int out2[] = {1, 2};
    EXPECT_TRUE(Eq(l, out2, 2));
    auto it = l.begin();
    ++it;
    auto r = l.insert(it, 7);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r.value(), 7);
    const int out3[] = {1, 7, 2};
    EXPECT_TRUE(Eq(l, out3, 3));
    l.erase(r.value());
    EXPECT_EQ(l.size(), 2u);
    auto rev = l.end();
    --rev;
    EXPECT_EQ(*rev, 2);
    auto c = l.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_TRUE(Eq(c.value(), out2, 2));
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(List, SortReverseUniqueRemoveMerge) {
  TestAllocator alloc;
  {
    List<int> l(alloc);
    const int in[] = {5, 3, 9, 3, 1, 5, 5, 7};
    Fill(l, in, 8);
    l.sort();
    const int sorted[] = {1, 3, 3, 5, 5, 5, 7, 9};
    EXPECT_TRUE(Eq(l, sorted, 8));
    EXPECT_EQ(l.unique(), 3u);
    const int uniq[] = {1, 3, 5, 7, 9};
    EXPECT_TRUE(Eq(l, uniq, 5));
    l.reverse();
    const int rev[] = {9, 7, 5, 3, 1};
    EXPECT_TRUE(Eq(l, rev, 5));
    EXPECT_EQ(l.remove(7), 1u);
    EXPECT_EQ(l.remove_if([](int x) { return x > 4; }), 2u);
    const int left[] = {3, 1};
    EXPECT_TRUE(Eq(l, left, 2));
    l.sort();
    List<int> o(alloc);
    const int oin[] = {0, 2, 4};
    Fill(o, oin, 3);
    l.merge(o);
    EXPECT_TRUE(o.empty());
    const int merged[] = {0, 1, 2, 3, 4};
    EXPECT_TRUE(Eq(l, merged, 5));
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(List, StableSortAndSplice) {
  TestAllocator alloc;
  List<Pair<int, int>> l(alloc);
  for (int i = 0; i < 20; ++i)
    ASSERT_TRUE(l.emplace_back(i % 3, i).has_value());
  l.sort([](const Pair<int, int> &a, const Pair<int, int> &b) {
    return a.first < b.first;
  });
  int pk = -1, pi = -1;
  for (auto &p : l) {
    if (p.first == pk) {
      EXPECT_GT(p.second, pi);
    }
    pk = p.first;
    pi = p.second;
  }

  List<int> a(alloc), b(alloc);
  const int av[] = {1, 2}, bv[] = {8, 9, 10};
  Fill(a, av, 2);
  Fill(b, bv, 3);
  auto pos = a.begin();
  ++pos;
  a.splice(pos, b);
  const int e1[] = {1, 8, 9, 10, 2};
  EXPECT_TRUE(Eq(a, e1, 5));
  EXPECT_TRUE(b.empty());
  List<int> c(alloc);
  const int cv[] = {100, 200};
  Fill(c, cv, 2);
  a.splice(a.begin(), c, c.begin());
  EXPECT_EQ(a.front(), 100);
  EXPECT_EQ(c.size(), 1u);
}

TEST(List, OomLeavesUnchanged) {
  Tr::alive = 0;
  TestAllocator alloc;
  {
    List<Tr> l(alloc);
    ASSERT_TRUE(l.emplace_back(1).has_value());
    alloc.fail = true;
    EXPECT_EQ(l.emplace_back(2).error(), CommonError::OutOfMemory);
    EXPECT_EQ(l.size(), 1u);
    alloc.fail = false;
  }
  EXPECT_EQ(Tr::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(Map, InsertFindEraseKeepsBalance) {
  TestAllocator alloc;
  {
    Map<int, int> m(alloc);
    for (int i = 0; i < 1000; ++i) {
      int k = (i * 389) % 1000;
      ASSERT_TRUE(m.try_emplace(k, k * 2).has_value());
      if (i % 100 == 0) {
        ASSERT_TRUE(m.CheckInvariants());
      }
    }
    EXPECT_EQ(m.size(), 1000u);
    EXPECT_TRUE(m.CheckInvariants());
    int prev = -1;
    for (auto &kv : m) {
      EXPECT_GT(kv.first, prev);
      EXPECT_EQ(kv.second, kv.first * 2);
      prev = kv.first;
    }
    for (int i = 0; i < 1000; i += 3)
      EXPECT_EQ(m.erase(i), 1u);
    EXPECT_EQ(m.erase(0), 0u);
    EXPECT_TRUE(m.CheckInvariants());
    EXPECT_EQ(m.size(), 666u);
    EXPECT_EQ(m.lower_bound(3)->first, 4);
    EXPECT_EQ(m.upper_bound(4)->first, 5);
    EXPECT_TRUE(m.find(3) == m.end());
    EXPECT_EQ(m.at(4), 8);
    auto it = m.end();
    --it;
    EXPECT_EQ(it->first, 998);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(Map, DuplicatesAssignStableAndErase) {
  TestAllocator alloc;
  Map<int, int> m(alloc);
  auto r1 = m.try_emplace(1, 10);
  ASSERT_TRUE(r1.has_value());
  EXPECT_TRUE(r1.value().second);
  int *addr = &r1.value().first->second;
  for (int i = 2; i < 200; ++i)
    ASSERT_TRUE(m.insert_or_assign(i, i).has_value());
  EXPECT_EQ(addr, &m.find(1)->second);
  auto r2 = m.try_emplace(1, 99);
  EXPECT_FALSE(r2.value().second);
  EXPECT_EQ(m.at(1), 10);
  auto r3 = m.insert_or_assign(1, 55);
  EXPECT_FALSE(r3.value().second);
  EXPECT_EQ(m.at(1), 55);
  EXPECT_EQ(m.checked_at(5000).error(), CommonError::ValueNotFound);
  auto it = m.erase(m.find(1));
  EXPECT_EQ(it->first, 2);
  auto c = m.Clone();
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c.value().size(), m.size());
  EXPECT_TRUE(c.value().CheckInvariants());
  Map<int, int> moved(Move(m));
  EXPECT_TRUE(m.empty());
  EXPECT_EQ(moved.size(), 198u);
}

TEST(Map, OomLeavesUnchangedAndDestroys) {
  Tr::alive = 0;
  TestAllocator alloc;
  {
    Map<int, Tr> m(alloc);
    for (int i = 0; i < 10; ++i)
      ASSERT_TRUE(m.try_emplace(i, i).has_value());
    alloc.fail = true;
    EXPECT_EQ(m.try_emplace(99, 1).error(), CommonError::OutOfMemory);
    EXPECT_EQ(m.size(), 10u);
    alloc.fail = false;
    EXPECT_EQ(Tr::alive, 10);
  }
  EXPECT_EQ(Tr::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(Set, OrderedUnique) {
  TestAllocator alloc;
  {
    Set<int> s(alloc);
    for (int i = 0; i < 300; ++i)
      ASSERT_TRUE(s.insert((i * 7) % 100).has_value());
    EXPECT_EQ(s.size(), 100u);
    EXPECT_TRUE(s.CheckInvariants());
    int expect = 0;
    for (int k : s)
      EXPECT_EQ(k, expect++);
    EXPECT_TRUE(s.contains(50));
    EXPECT_EQ(s.erase(50), 1u);
    EXPECT_FALSE(s.contains(50));
    EXPECT_EQ(*s.lower_bound(50), 51);
    auto it = s.erase(s.find(10));
    EXPECT_EQ(*it, 11);
    EXPECT_TRUE(s.CheckInvariants());
    auto c = s.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().size(), 98u);
    Set<int> t(alloc);
    s.swap(t);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(t.size(), 98u);
  }
  EXPECT_EQ(alloc.live, 0);
}
