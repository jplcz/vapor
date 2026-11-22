#include <vapor/array.hpp>

#include <gtest/gtest.h>

using vapor::Array;

TEST(ArrayTest, AggregateInitAndAccess) {
  Array<int, 3> a = {{1, 2, 3}};
  EXPECT_EQ(a.size(), 3U);
  EXPECT_FALSE(a.empty());
  EXPECT_EQ(a[1], 2);
  EXPECT_EQ(a.front(), 1);
  EXPECT_EQ(a.back(), 3);
  EXPECT_EQ(a.at(2), 3);
  int sum = 0;
  for (int x : a)
    sum += x;
  EXPECT_EQ(sum, 6);
}

TEST(ArrayTest, CheckedAtFillSwapCompare) {
  Array<int, 2> a = {{1, 2}};
  Array<int, 2> b = {{3, 4}};
  EXPECT_FALSE(a.checked_at(2));
  EXPECT_EQ(a.checked_at(1).value().get(), 2);
  a.swap(b);
  EXPECT_EQ(a[0], 3);
  b.fill(7);
  EXPECT_EQ(b[1], 7);
  EXPECT_TRUE(a != b);
  Array<int, 2> c = {{3, 4}};
  EXPECT_TRUE(a == c);
}

TEST(ArrayTest, ZeroLengthAndSpan) {
  Array<int, 0> z;
  EXPECT_TRUE(z.empty());
  EXPECT_EQ(z.begin(), z.end());
  EXPECT_FALSE(z.checked_at(0));

  Array<int, 3> a = {{1, 2, 3}};
  vapor::Span<int> s = a;
  EXPECT_EQ(s.size(), 3U);
  const Array<int, 3> &ca = a;
  vapor::Span<const int> cs = ca;
  EXPECT_EQ(cs[2], 3);
}
