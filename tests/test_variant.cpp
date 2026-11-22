#include <vapor/variant.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using namespace vapor;
using vapor_test::Tracked;

namespace {
struct Visitor {
  int operator()(Monostate) const { return 0; }
  int operator()(int v) const { return v; }
  int operator()(float) const { return -1; }
};
struct Mutator {
  void operator()(Monostate) const {}
  void operator()(int &v) const { v += 100; }
  void operator()(float &) const {}
};
} // namespace

TEST(VariantTest, DefaultsToFirstAlternative) {
  Variant<int, float> v;
  EXPECT_EQ(v.index(), 0U);
  EXPECT_EQ(Get<int>(v), 0);
  Variant<Monostate, int> m;
  EXPECT_TRUE(HoldsAlternative<Monostate>(m));
}

TEST(VariantTest, ConvertingConstructionAndAssignment) {
  Variant<Monostate, int, float> v(3);
  EXPECT_EQ(v.index(), 1U);
  v = 2.5f;
  EXPECT_EQ(v.index(), 2U);
  EXPECT_EQ(Get<float>(v), 2.5f);
  v = 8;
  EXPECT_EQ(Get<1>(v), 8);
  v = 9; // same alternative: assigned in place
  EXPECT_EQ(Get<int>(v), 9);
}

TEST(VariantTest, GetIfReturnsNullForOtherAlternative) {
  Variant<int, float> v(1);
  EXPECT_NE(GetIf<int>(&v), nullptr);
  EXPECT_EQ(GetIf<float>(&v), nullptr);
  const Variant<int, float> &c = v;
  EXPECT_EQ(*GetIf<0>(&c), 1);
  Variant<int, float> *none = nullptr;
  EXPECT_EQ(GetIf<int>(none), nullptr);
}

TEST(VariantDeathTest, GetOnWrongAlternativeAborts) {
  Variant<int, float> v(1);
  EXPECT_DEATH({ (void)Get<float>(v); }, "");
}

TEST(VariantTest, VisitCallsMatchingOverload) {
  Variant<Monostate, int, float> v;
  EXPECT_EQ(Visit(Visitor(), v), 0);
  v = 5;
  EXPECT_EQ(Visit(Visitor(), v), 5);
  v = 1.0f;
  EXPECT_EQ(Visit(Visitor(), v), -1);
  v = 1;
  Visit(Mutator(), v);
  EXPECT_EQ(Get<int>(v), 101);
  const Variant<Monostate, int, float> &c = v;
  EXPECT_EQ(Visit(Visitor(), c), 101);
}

TEST(VariantTest, InPlaceConstructionAndEmplace) {
  Variant<int, Tracked> v(InPlaceTypeTag<Tracked>(), 7);
  EXPECT_EQ(Get<Tracked>(v).value, 7);
  v.emplace<0>(3);
  EXPECT_EQ(Get<int>(v), 3);
  v.emplace<Tracked>(9);
  EXPECT_EQ(Get<1>(v).value, 9);
  Variant<int, Tracked> w(InPlaceIndexTag<0>(), 4);
  EXPECT_EQ(Get<int>(w), 4);
}

TEST(VariantTest, ComparisonOrdersByIndexThenValue) {
  Variant<int, float> a(1), b(1), c(2), d(0.5f);
  EXPECT_TRUE(a == b);
  EXPECT_TRUE(a != c);
  EXPECT_TRUE(a != d);
  EXPECT_TRUE(a < c);
  EXPECT_TRUE(c < d); // int alternative sorts before float
  EXPECT_FALSE(d < a);
}

TEST(VariantTest, CopyMoveAndSwap) {
  Variant<int, float> a(1.5f), b(2);
  Variant<int, float> c(a);
  EXPECT_TRUE(c == a);
  b = a;
  EXPECT_EQ(Get<float>(b), 1.5f);
  Variant<int, float> m(Move(c));
  EXPECT_EQ(Get<float>(m), 1.5f);
  Variant<int, float> x(1), y(2.0f);
  Swap(x, y);
  EXPECT_EQ(Get<float>(x), 2.0f);
  EXPECT_EQ(Get<int>(y), 1);
  b = b; // self-assignment is harmless
  EXPECT_EQ(Get<float>(b), 1.5f);
}

TEST(VariantTest, LifetimesAreBalanced) {
  Tracked::alive = 0;
  {
    Variant<int, Tracked> v(InPlaceTypeTag<Tracked>(), 1);
    EXPECT_EQ(Tracked::alive, 1);
    Variant<int, Tracked> copy(v);
    EXPECT_EQ(Tracked::alive, 2);
    Variant<int, Tracked> moved(Move(copy));
    EXPECT_EQ(Tracked::alive, 3); // moved-from Tracked is still alive
    v = 5;                        // destroys the Tracked it held
    EXPECT_EQ(Tracked::alive, 2);
    v = moved;
    EXPECT_EQ(Tracked::alive, 3);
    moved = Variant<int, Tracked>(2);
    EXPECT_EQ(Tracked::alive, 2);
  }
  EXPECT_EQ(Tracked::alive, 0);
}

TEST(VariantTest, SizeAndAlternativeTraits) {
  typedef Variant<char, double> V;
  static_assert(VariantSize<V>::value == 2, "size");
  static_assert(IsSame<VariantAlternativeT<1, V>, double>::value, "alt");
  EXPECT_GE(sizeof(V), sizeof(double));
  EXPECT_EQ(alignof(V), alignof(double));
}
