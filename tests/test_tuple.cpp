#include <vapor/tuple.hpp>

#include <gtest/gtest.h>

using vapor::Get;
using vapor::MakePair;
using vapor::MakeTuple;
using vapor::Pair;
using vapor::Tie;
using vapor::Tuple;

namespace {
struct Empty1 {};
struct Empty2 {};

struct MoveOnly {
  int v;
  explicit MoveOnly(int x) : v(x) {}
  MoveOnly(MoveOnly &&o) noexcept : v(o.v) { o.v = -1; }
  MoveOnly &operator=(MoveOnly &&o) noexcept {
    v = o.v;
    o.v = -1;
    return *this;
  }
  MoveOnly(const MoveOnly &) = delete;
};
} // namespace

TEST(PairTest, ConstructAssignCompare) {
  Pair<int, float> p(1, 2.5f);
  EXPECT_EQ(p.first, 1);
  EXPECT_EQ(p.second, 2.5f);
  Pair<long, double> q(p); // converting copy
  EXPECT_EQ(q.first, 1L);
  EXPECT_TRUE(p == MakePair(1, 2.5f));
  EXPECT_TRUE(p < MakePair(1, 3.0f));
  EXPECT_TRUE(MakePair(0, 9.0f) < p);
  EXPECT_TRUE(p != MakePair(2, 2.5f));
  Pair<int, float> z;
  EXPECT_EQ(z.first, 0);
  z = p;
  EXPECT_EQ(z.second, 2.5f);
  Pair<int, float> y(7, 8.0f);
  vapor::Swap(y, z);
  EXPECT_EQ(y.first, 1);
  EXPECT_EQ(z.first, 7);
}

TEST(PairTest, MoveOnlyAndGet) {
  Pair<MoveOnly, int> p(MoveOnly(4), 5);
  Pair<MoveOnly, int> q(vapor::Move(p));
  EXPECT_EQ(q.first.v, 4);
  EXPECT_EQ(Get<1>(q), 5);
  Get<1>(q) = 6;
  EXPECT_EQ(q.second, 6);
  MoveOnly m = Get<0>(vapor::Move(q));
  EXPECT_EQ(m.v, 4);
  static_assert(vapor::TupleSize<Pair<int, int>>::value == 2, "");
}

#if __cplusplus >= 201703L
TEST(PairTest, StructuredBindings) {
  Pair<int, int> p(3, 4);
  auto &[a, b] = p;
  EXPECT_EQ(a + b, 7);
}
#endif

TEST(TupleTest, GetByIndexAndType) {
  Tuple<int, float, bool> t(1, 2.0f, true);
  EXPECT_EQ(Get<0>(t), 1);
  EXPECT_EQ(Get<float>(t), 2.0f);
  EXPECT_TRUE(Get<bool>(t));
  Get<0>(t) = 9;
  const Tuple<int, float, bool> &ct = t;
  EXPECT_EQ(Get<0>(ct), 9);
  static_assert(vapor::TupleSize<Tuple<int, float, bool>>::value == 3, "");
  static_assert(
      vapor::IsSame<vapor::TupleElementT<1, Tuple<int, float>>, float>::value,
      "");
}

TEST(TupleTest, EmptyElementsTakeNoSpace) {
  static_assert(sizeof(Tuple<Empty1, int, Empty2>) == sizeof(int), "");
  Tuple<Empty1, int, Empty2> t;
  Get<1>(t) = 5;
  EXPECT_EQ(Get<1>(t), 5);
}

TEST(TupleTest, ConvertCopyMoveAndAssign) {
  Tuple<int, float> a(1, 2.0f);
  Tuple<long, double> b(a);
  EXPECT_EQ(Get<0>(b), 1L);
  Tuple<int, float> c(a);
  EXPECT_TRUE(c == a);

  Tuple<MoveOnly, int> m(MoveOnly(3), 4);
  Tuple<MoveOnly, int> n(vapor::Move(m));
  EXPECT_EQ(Get<0>(n).v, 3);
  EXPECT_EQ(Get<0>(m).v, -1);
  m = vapor::Move(n);
  EXPECT_EQ(Get<0>(m).v, 3);

  Tuple<int> one(5);
  Tuple<int> copy(one); // must not pick the forwarding constructor
  EXPECT_EQ(Get<0>(copy), 5);
}

TEST(TupleTest, TieAssignsThrough) {
  int x = 0;
  float y = 0;
  Tie(x, y) = MakeTuple(3, 4.0f);
  EXPECT_EQ(x, 3);
  EXPECT_EQ(y, 4.0f);
  Tie(x, y) = Tuple<int, float>(5, 6.0f);
  EXPECT_EQ(x, 5);
  int u = 1, w = 2;
  Tuple<int &, int &> r = Tie(u, w);
  Tuple<int &, int &> r2 = Tie(x, x);
  r2 = r; // assigns the referenced ints
  EXPECT_EQ(x, 2);
}

TEST(TupleTest, CompareSwapApply) {
  Tuple<int, int> a(1, 2), b(1, 3);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(b >= a);
  vapor::Swap(a, b);
  EXPECT_EQ(Get<1>(a), 3);
  Tuple<> e1, e2;
  EXPECT_TRUE(e1 == e2);

  struct Sum {
    int operator()(int p, int q) const { return p + q; }
  };
  EXPECT_EQ(vapor::Apply(Sum(), a), 4);
  EXPECT_EQ(vapor::Apply(Sum(), MakePair(5, 6)), 11);
}
