#include <vapor/algorithm.hpp>
#include <vapor/priority_queue.hpp>
#include <vapor/vector.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using namespace vapor;

namespace {
Vector<int> Pseudo(int n) {
  Vector<int> v;
  for (int i = 0; i < n; ++i)
    EXPECT_TRUE(v.push_back((i * 7919) % 503).has_value());
  return v;
}
} // namespace

TEST(HeapTest, MakePushPopSortHeap) {
  Vector<int> v = Pseudo(200);
  MakeHeap(v);
  EXPECT_TRUE(IsHeap(v));

  ASSERT_TRUE(v.push_back(10000).has_value());
  PushHeap(v);
  EXPECT_EQ(v[0], 10000);
  EXPECT_TRUE(IsHeap(v));

  PopHeap(v);
  EXPECT_EQ(v.back(), 10000);
  v.pop_back();
  EXPECT_TRUE(IsHeap(v));

  SortHeap(v);
  EXPECT_TRUE(IsSorted(v));
}

TEST(HeapTest, CustomComparatorMakesMinHeap) {
  Vector<int> v = Pseudo(50);
  MakeHeap(v, Greater<int>());
  EXPECT_TRUE(IsHeap(v, Greater<int>()));
  int smallest = v[0];
  for (size_t i = 0; i < v.size(); ++i)
    EXPECT_LE(smallest, v[i]);
}

TEST(HeapTest, IsHeapUntilFindsViolation) {
  int a[] = {9, 5, 8, 1, 2, 100};
  EXPECT_FALSE(IsHeap(a, a + 6));
  EXPECT_EQ(IsHeapUntil(a, a + 6, Less<int>()), a + 5);
  EXPECT_TRUE(IsHeap(a, a + 5));
}

TEST(HeapTest, TinyRangesAreNoOps) {
  int one[] = {1};
  MakeHeap(one, one + 1);
  PushHeap(one, one + 1);
  PopHeap(one, one + 1);
  SortHeap(one, one + 1);
  EXPECT_EQ(one[0], 1);
  EXPECT_TRUE(IsHeap(one, one));
}

TEST(PriorityQueueTest, PopsLargestFirst) {
  PriorityQueue<int> q;
  EXPECT_TRUE(q.empty());
  int xs[] = {5, 1, 9, 3, 7};
  for (int x : xs)
    ASSERT_TRUE(q.push(x).has_value());
  EXPECT_EQ(q.size(), 5U);
  int expected[] = {9, 7, 5, 3, 1};
  for (int e : expected) {
    EXPECT_EQ(q.top(), e);
    q.pop();
  }
  EXPECT_TRUE(q.empty());
}

TEST(PriorityQueueTest, GreaterGivesSmallestFirstAndCheckedTop) {
  PriorityQueue<int, Greater<int>> q;
  EXPECT_EQ(q.checked_top().error(), CommonError::OutOfBounds);
  ASSERT_TRUE(q.push(4).has_value());
  ASSERT_TRUE(q.emplace(2).has_value());
  auto top = q.checked_top();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(top.value().get(), 2);
}

TEST(PriorityQueueTest, ReportsOutOfMemory) {
  vapor_test::TestAllocator alloc;
  PriorityQueue<int> q(alloc);
  ASSERT_TRUE(q.push(1).has_value());
  alloc.fail = true;
  // Growth only allocates when capacity is exhausted; push until it must.
  Expected<void, CommonError> r;
  for (int i = 0; i < 1000 && r.has_value(); ++i)
    r = q.push(i);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), CommonError::OutOfMemory);
  alloc.fail = false;
}

TEST(PriorityQueueTest, MovesElementsAndDestroysThem) {
  using vapor_test::Tracked;
  struct ByValue {
    bool operator()(const Tracked &a, const Tracked &b) const {
      return a.value < b.value;
    }
  };
  Tracked::alive = 0;
  {
    PriorityQueue<Tracked, ByValue> q;
    for (int i = 0; i < 20; ++i)
      ASSERT_TRUE(q.emplace(i).has_value());
    EXPECT_EQ(Tracked::alive, 20);
    EXPECT_EQ(q.top().value, 19);
    q.pop();
    EXPECT_EQ(Tracked::alive, 19);
  }
  EXPECT_EQ(Tracked::alive, 0);
}
