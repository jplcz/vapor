#include <vapor/comparators.hpp>
#include <vapor/flat_map.hpp>
#include <vapor/flat_set.hpp>
#include <vapor/string.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using namespace vapor;
using vapor_test::TestAllocator;

TEST(FlatMap, StaysSortedAndFinds) {
  TestAllocator alloc;
  {
    FlatMap<int, int> m(alloc);
    for (int i = 0; i < 200; ++i) {
      int k = (i * 37) % 200;
      ASSERT_TRUE(m.try_emplace(k, k * 2).has_value());
    }
    EXPECT_EQ(m.size(), 200u);
    int prev = -1;
    for (auto &kv : m) {
      EXPECT_GT(kv.first, prev);
      EXPECT_EQ(kv.second, kv.first * 2);
      prev = kv.first;
    }
    EXPECT_EQ(m.find(150)->second, 300);
    EXPECT_TRUE(m.find(1000) == m.end());
    EXPECT_EQ(m.erase(150), 1u);
    EXPECT_EQ(m.erase(150), 0u);
    EXPECT_FALSE(m.contains(150));
    EXPECT_EQ(m.lower_bound(150)->first, 151);
    EXPECT_EQ(m.upper_bound(149)->first, 151);
    auto er = m.equal_range(10);
    EXPECT_EQ(er.second - er.first, 1);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(FlatMap, DuplicatesAssignAndAt) {
  FlatMap<int, int> m;
  EXPECT_TRUE(m.try_emplace(1, 10).value().second);
  EXPECT_FALSE(m.try_emplace(1, 99).value().second);
  EXPECT_EQ(m.at(1), 10);
  EXPECT_FALSE(m.insert_or_assign(1, 5).value().second);
  EXPECT_EQ(m.at(1), 5);
  EXPECT_TRUE(m.insert_or_assign(2, 6).value().second);
  EXPECT_TRUE(m.emplace(3, 7).value().second);
  EXPECT_EQ(m.checked_at(9).error(), CommonError::ValueNotFound);
  EXPECT_EQ(m.checked_at(3).value().get(), 7);
  auto it = m.erase(m.find(2));
  EXPECT_EQ(it->first, 3);
}

TEST(FlatMap, CustomOrderStringValuesCloneAndOom) {
  TestAllocator alloc;
  {
    FlatMap<int, String, Greater<int>> m(alloc);
    for (int i = 0; i < 10; ++i) {
      String s(alloc);
      ASSERT_TRUE(s.append(StringView("v")).has_value());
      ASSERT_TRUE(m.insert_or_assign(i, Move(s)).has_value());
    }
    EXPECT_EQ(m.begin()->first, 9);
    EXPECT_EQ(m.at(3).size(), 1u);

    FlatMap<int, int> small(alloc);
    ASSERT_TRUE(small.try_emplace(1, 1).has_value());
    auto c = small.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().at(1), 1);
    alloc.fail = true;
    bool failed = false;
    for (int i = 2; i < 100 && !failed; ++i)
      failed = !small.try_emplace(i, i).has_value();
    EXPECT_TRUE(failed);
    alloc.fail = false;
    EXPECT_EQ(small.at(1), 1);
    FlatMap<int, int> other(alloc);
    small.swap(other);
    EXPECT_TRUE(small.empty());
    EXPECT_TRUE(other.contains(1));
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(FlatSet, Basics) {
  TestAllocator alloc;
  {
    FlatSet<int> s(alloc);
    for (int i = 0; i < 100; ++i)
      ASSERT_TRUE(s.insert((i * 7) % 50).has_value());
    EXPECT_EQ(s.size(), 50u);
    int prev = -1;
    for (int k : s) {
      EXPECT_GT(k, prev);
      prev = k;
    }
    EXPECT_FALSE(s.insert(3).value().second);
    EXPECT_TRUE(s.contains(49));
    EXPECT_EQ(s.erase(49), 1u);
    EXPECT_FALSE(s.contains(49));
    EXPECT_EQ(*s.lower_bound(10), 10);
    EXPECT_EQ(*s.upper_bound(10), 11);
    auto c = s.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().size(), 49u);
    s.clear();
    EXPECT_TRUE(s.empty());
  }
  EXPECT_EQ(alloc.live, 0);
}
