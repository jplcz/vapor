#include <vapor/inline_vector.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using vapor::CommonError;
using vapor::InlineVector;
using vapor_test::Tracked;

TEST(InlineVectorTest, FillsUpToCapacityThenFails) {
  InlineVector<int, 3> v;
  EXPECT_EQ(v.capacity(), 3U);
  for (int i = 0; i < 3; ++i)
    ASSERT_TRUE(v.push_back(i));
  EXPECT_TRUE(v.full());
  auto res = v.push_back(9);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), CommonError::CapacityExceeded);
  EXPECT_EQ(v.size(), 3U);
  EXPECT_FALSE(v.resize(4));
  int more[] = {1};
  EXPECT_FALSE(v.append(vapor::Span<const int>(more, 1)));
}

TEST(InlineVectorTest, LifetimesAreBalanced) {
  Tracked::alive = 0;
  {
    InlineVector<Tracked, 8> v;
    for (int i = 0; i < 5; ++i)
      ASSERT_TRUE(v.emplace_back(i));
    EXPECT_EQ(Tracked::alive, 5);
    v.erase(v.begin() + 1);
    EXPECT_EQ(Tracked::alive, 4);
    ASSERT_TRUE(v.insert(v.begin() + 1, Tracked(7)));
    EXPECT_EQ(v[1].value, 7);
    EXPECT_EQ(v[2].value, 2);
    ASSERT_TRUE(v.insert(v.begin(), v[3])); // aliasing insert
    EXPECT_EQ(v[0].value, 3);
    v.pop_back();
    ASSERT_TRUE(v.resize(2).has_value());
    EXPECT_EQ(Tracked::alive, 2);
  }
  EXPECT_EQ(Tracked::alive, 0);
}

TEST(InlineVectorTest, CopyAndMove) {
  Tracked::alive = 0;
  InlineVector<Tracked, 4> a;
  ASSERT_TRUE(a.emplace_back(1));
  ASSERT_TRUE(a.emplace_back(2));

  InlineVector<Tracked, 4> copy(a);
  EXPECT_EQ(copy.size(), 2U);
  EXPECT_EQ(a.size(), 2U);

  InlineVector<Tracked, 4> moved(vapor::Move(a));
  EXPECT_TRUE(a.empty());
  EXPECT_EQ(moved[1].value, 2);

  copy = moved;
  EXPECT_TRUE(copy == moved);
  moved.clear();
  EXPECT_EQ(Tracked::alive, 2);
}

TEST(InlineVectorTest, CheckedAccessAndSpan) {
  InlineVector<int, 4> v;
  ASSERT_TRUE(v.push_back(3));
  EXPECT_EQ(v.checked_at(0).value().get(), 3);
  EXPECT_FALSE(v.checked_at(1));
  vapor::Span<int> s = v;
  EXPECT_EQ(s.size(), 1U);
  int extra[] = {4, 5};
  ASSERT_TRUE(v.append(vapor::Span<const int>(extra, 2)));
  EXPECT_EQ(v.back(), 5);
  InlineVector<int, 4> other;
  ASSERT_TRUE(other.push_back(1));
  v.swap(other);
  EXPECT_EQ(v.size(), 1U);
  EXPECT_EQ(other.size(), 3U);
}
