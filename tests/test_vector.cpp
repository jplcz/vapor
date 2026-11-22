#include <vapor/vector.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using vapor::Vector;
using vapor_test::Tracked;
using vapor_test::TestAllocator;

TEST(VectorTest, PushBackGrowsAndKeepsOrder) {
  TestAllocator alloc;
  {
    Vector<int> v(alloc);
    for (int i = 0; i < 100; ++i)
      ASSERT_TRUE(v.push_back(i));
    EXPECT_EQ(v.size(), 100U);
    EXPECT_GE(v.capacity(), 100U);
    for (int i = 0; i < 100; ++i)
      EXPECT_EQ(v[i], i);
    EXPECT_EQ(v.front(), 0);
    EXPECT_EQ(v.back(), 99);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(VectorTest, OutOfMemoryLeavesVectorUnchanged) {
  TestAllocator alloc;
  Vector<int> v(alloc);
  ASSERT_TRUE(v.push_back(1));
  ASSERT_TRUE(v.reserve(v.capacity()));
  while (v.size() < v.capacity())
    ASSERT_TRUE(v.push_back(7));

  alloc.fail = true;
  size_t before = v.size();
  auto res = v.push_back(2);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), vapor::CommonError::OutOfMemory);
  EXPECT_EQ(v.size(), before);
  EXPECT_EQ(v[0], 1);
  EXPECT_FALSE(v.reserve(v.capacity() * 4));
}

TEST(VectorTest, NonTrivialElementsAreMovedAndDestroyed) {
  TestAllocator alloc;
  Tracked::alive = 0;
  {
    Vector<Tracked> v(alloc);
    for (int i = 0; i < 20; ++i)
      ASSERT_TRUE(v.emplace_back(i));
    EXPECT_EQ(Tracked::alive, 20);
    for (int i = 0; i < 20; ++i)
      EXPECT_EQ(v[i].value, i);
    v.pop_back();
    EXPECT_EQ(Tracked::alive, 19);
    v.clear();
    EXPECT_EQ(Tracked::alive, 0);
    ASSERT_TRUE(v.emplace_back(5));
  }
  EXPECT_EQ(Tracked::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(VectorTest, PushBackOfOwnElementSurvivesReallocation) {
  Vector<Tracked> v;
  ASSERT_TRUE(v.emplace_back(42));
  while (v.size() < v.capacity())
    ASSERT_TRUE(v.emplace_back(1));
  ASSERT_TRUE(v.push_back(v[0])); // forces growth
  EXPECT_EQ(v.back().value, 42);
}

TEST(VectorTest, InsertAndErase) {
  Vector<Tracked> v;
  for (int i : {1, 2, 4, 5})
    ASSERT_TRUE(v.emplace_back(i));
  auto it = v.insert(v.begin() + 2, Tracked(3));
  ASSERT_TRUE(it);
  EXPECT_EQ((*it)->value, 3);
  for (size_t i = 0; i < 5; ++i)
    EXPECT_EQ(v[i].value, static_cast<int>(i) + 1);

  ASSERT_TRUE(v.insert(v.begin(), v[4])); // aliasing insert
  EXPECT_EQ(v[0].value, 5);

  auto after = v.erase(v.begin());
  EXPECT_EQ(after->value, 1);
  v.erase(v.begin() + 1, v.begin() + 3);
  EXPECT_EQ(v.size(), 3U);
  EXPECT_EQ(v[0].value, 1);
  EXPECT_EQ(v[1].value, 4);
  EXPECT_EQ(v[2].value, 5);

  EXPECT_FALSE(v.insert(v.end() + 1, Tracked(0)));
}

TEST(VectorTest, ResizeAssignsDefaultsAndValues) {
  Vector<int> v;
  ASSERT_TRUE(v.resize(3));
  EXPECT_EQ(v[2], 0);
  ASSERT_TRUE(v.resize(5, 9));
  EXPECT_EQ(v[4], 9);
  ASSERT_TRUE(v.resize(1));
  EXPECT_EQ(v.size(), 1U);
}

TEST(VectorTest, AppendRangeIncludingOwnContents) {
  Vector<int> v;
  int a[] = {1, 2, 3, 4};
  ASSERT_TRUE(v.append(vapor::Span<const int>(a, 4)));
  ASSERT_TRUE(v.shrink_to_fit());
  ASSERT_EQ(v.capacity(), 4U);
  ASSERT_TRUE(v.append(vapor::Span<const int>(v.data(), v.size())));
  EXPECT_EQ(v.size(), 8U);
  EXPECT_EQ(v[4], 1);
  EXPECT_EQ(v[7], 4);
}

TEST(VectorTest, CloneIsDeepAndFallible) {
  TestAllocator alloc;
  Vector<Tracked> v(alloc);
  ASSERT_TRUE(v.emplace_back(1));
  ASSERT_TRUE(v.emplace_back(2));

  auto copy = v.Clone();
  ASSERT_TRUE(copy);
  EXPECT_EQ(copy->size(), 2U);
  EXPECT_NE(copy->data(), v.data());
  EXPECT_EQ((*copy)[1].value, 2);

  alloc.fail = true;
  EXPECT_FALSE(v.Clone());
}

TEST(VectorTest, MoveTransfersOwnership) {
  Vector<int> a;
  ASSERT_TRUE(a.push_back(1));
  Vector<int> b(vapor::Move(a));
  EXPECT_TRUE(a.empty());
  EXPECT_EQ(b.size(), 1U);
  a = vapor::Move(b);
  EXPECT_EQ(a.size(), 1U);
  EXPECT_TRUE(b.empty());
}

TEST(VectorTest, CheckedAccessAndSpan) {
  Vector<int> v;
  ASSERT_TRUE(v.push_back(5));
  EXPECT_EQ(v.checked_at(0).value().get(), 5);
  auto bad = v.checked_at(1);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error(), vapor::CommonError::OutOfBounds);

  vapor::Span<int> s = v;
  EXPECT_EQ(s.size(), 1U);
  const Vector<int> &cv = v;
  vapor::Span<const int> cs = cv;
  EXPECT_EQ(cs[0], 5);
}

TEST(VectorTest, RangeForAndEquality) {
  Vector<int> a, b;
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(a.push_back(i));
    ASSERT_TRUE(b.push_back(i));
  }
  int sum = 0;
  for (int x : a)
    sum += x;
  EXPECT_EQ(sum, 3);
  EXPECT_TRUE(a == b);
  ASSERT_TRUE(b.push_back(1));
  EXPECT_TRUE(a != b);
}
