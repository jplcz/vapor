#include <vapor/iterator.hpp>
#include <vapor/vector.hpp>

#include <gtest/gtest.h>

using namespace vapor;

namespace {
// Forward-only iterator over ints, to exercise the non-random-access paths.
struct CountingIterator {
  int v;
  int &operator*() { return v; }
  CountingIterator &operator++() {
    ++v;
    return *this;
  }
  CountingIterator &operator--() {
    --v;
    return *this;
  }
  bool operator!=(const CountingIterator &o) const { return v != o.v; }
};

Vector<int> Make(int n) {
  Vector<int> v;
  for (int i = 0; i < n; ++i)
    EXPECT_TRUE(v.push_back(i).has_value());
  return v;
}
} // namespace

TEST(IteratorTest, DistanceAdvanceNextPrev) {
  Vector<int> v = Make(5);
  EXPECT_EQ(Distance(v.begin(), v.end()), 5);
  EXPECT_EQ(*Next(v.begin(), 2), 2);
  EXPECT_EQ(*Prev(v.end()), 4);
  auto it = v.begin();
  Advance(it, 3);
  EXPECT_EQ(*it, 3);

  CountingIterator a = {0}, b = {4};
  EXPECT_EQ(Distance(a, b), 4);
  EXPECT_EQ(Next(a, 3).v, 3);
  EXPECT_EQ(Prev(b, 2).v, 2);
  static_assert(IsRandomAccessIterator<int *>::value, "pointer");
  static_assert(!IsRandomAccessIterator<CountingIterator>::value, "counting");
  static_assert(IsBidirectionalIterator<CountingIterator>::value, "counting");
}

TEST(IteratorTest, ReversedWalksBackwards) {
  Vector<int> v = Make(4);
  int expected = 3;
  for (int x : Reversed(v))
    EXPECT_EQ(x, expected--);
  EXPECT_EQ(expected, -1);

  ReverseIterator<int *> r = MakeReverseIterator(v.end());
  EXPECT_EQ(*r, 3);
  EXPECT_EQ(r[1], 2);
  EXPECT_EQ(*(r + 2), 1);
  ++r;
  EXPECT_EQ(*r, 2);
  EXPECT_EQ(*r.base(), 3);
}

TEST(IteratorTest, ReversedAllowsMutation) {
  Vector<int> v = Make(3);
  for (int &x : Reversed(v))
    x *= 10;
  EXPECT_EQ(v[2], 20);
}

TEST(IteratorTest, EnumerateGivesIndexAndReference) {
  Vector<int> v = Make(4);
  size_t n = 0;
  for (auto e : Enumerate(v)) {
    EXPECT_EQ(e.index, n);
    EXPECT_EQ(e.value, static_cast<int>(n));
    e.value += 1;
    ++n;
  }
  EXPECT_EQ(n, 4U);
  EXPECT_EQ(v[0], 1);
}

TEST(IteratorTest, EmptyRangesDoNothing) {
  Vector<int> v;
  for (int x : Reversed(v)) {
    (void)x;
    FAIL();
  }
  for (auto e : Enumerate(v)) {
    (void)e;
    FAIL();
  }
  EXPECT_TRUE(MakeRange(v.begin(), v.end()).empty());
}

TEST(IteratorTest, IteratorRangeSize) {
  Vector<int> v = Make(6);
  EXPECT_EQ(MakeRange(v.begin() + 1, v.end()).size(), 5U);
}
