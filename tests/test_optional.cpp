#include <vapor/optional.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using vapor::NullOpt;
using vapor::Optional;
using vapor_test::Tracked;

TEST(OptionalTest, EmptyAndEngaged) {
  Optional<int> o;
  EXPECT_FALSE(o);
  EXPECT_TRUE(o == NullOpt);
  EXPECT_EQ(o.value_or(7), 7);

  o = Optional<int>(5);
  ASSERT_TRUE(o.has_value());
  EXPECT_EQ(*o, 5);
  EXPECT_EQ(o.value(), 5);
  EXPECT_TRUE(o == 5);
  EXPECT_TRUE(o != NullOpt);

  o = NullOpt;
  EXPECT_FALSE(o.has_value());
}

TEST(OptionalTest, CheckedValueReportsEmpty) {
  Optional<int> o;
  auto res = o.checked_value();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), vapor::CommonError::EmptyOptional);
  o.emplace(3);
  EXPECT_EQ(o.checked_value().value().get(), 3);
}

TEST(OptionalTest, LifetimesAreBalanced) {
  Tracked::alive = 0;
  {
    Optional<Tracked> a(Tracked(1));
    EXPECT_EQ(Tracked::alive, 1);
    Optional<Tracked> b(a);
    EXPECT_EQ(Tracked::alive, 2);
    Optional<Tracked> c(vapor::Move(a));
    EXPECT_EQ(c->value, 1);
    b = NullOpt;
    EXPECT_EQ(Tracked::alive, 2); // a (moved-from) and c
    c.emplace(9);
    EXPECT_EQ(c->value, 9);
    c.reset();
    EXPECT_EQ(Tracked::alive, 1);
    b = a; // assign into empty
    EXPECT_EQ(Tracked::alive, 2);
    b = a; // assign onto engaged
    EXPECT_EQ(Tracked::alive, 2);
  }
  EXPECT_EQ(Tracked::alive, 0);
}

TEST(OptionalTest, MovesOutOfRvalue) {
  Tracked::copies = 0;
  Optional<Tracked> a(Tracked(4));
  Tracked t = vapor::Move(a).value();
  EXPECT_EQ(t.value, 4);
  EXPECT_EQ(Tracked::copies, 0);
  Optional<Tracked> b(Tracked(6));
  EXPECT_EQ(vapor::Move(b).value_or(Tracked(0)).value, 6);
  EXPECT_EQ(Tracked::copies, 0);
}

TEST(OptionalTest, SwapAndCompare) {
  Optional<int> a(1), b;
  a.swap(b);
  EXPECT_FALSE(a);
  EXPECT_EQ(*b, 1);
  Optional<int> c(1);
  EXPECT_TRUE(b == c);
  EXPECT_TRUE(a != c);
}
