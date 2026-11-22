#include <vapor/hash_map.hpp>
#include <vapor/hash_set.hpp>
#include <vapor/string.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using namespace vapor;
using vapor_test::TestAllocator;

namespace {
String MakeStr(const char *s, Allocator &a) {
  String str(a);
  EXPECT_TRUE(str.append(StringView(s)).has_value());
  return str;
}
} // namespace

TEST(HashMap, InsertFindEraseGrowth) {
  TestAllocator alloc;
  {
    HashMap<int, int> m(alloc);
    for (int i = 0; i < 1000; ++i) {
      auto r = m.try_emplace(i, i * 2);
      ASSERT_TRUE(r.has_value());
      EXPECT_TRUE(r.value().second);
    }
    EXPECT_EQ(m.size(), 1000u);
    for (int i = 0; i < 1000; ++i) {
      auto it = m.find(i);
      ASSERT_TRUE(it != m.end());
      EXPECT_EQ(it->second, i * 2);
    }
    EXPECT_TRUE(m.find(5000) == m.end());
    for (int i = 0; i < 1000; i += 2)
      EXPECT_EQ(m.erase(i), 1u);
    EXPECT_EQ(m.erase(0), 0u);
    EXPECT_EQ(m.size(), 500u);
    for (int i = 0; i < 1000; ++i)
      EXPECT_EQ(m.contains(i), i % 2 == 1);
    // Re-insert after erasing (tombstone reuse) and churn.
    for (int round = 0; round < 20; ++round) {
      for (int i = 0; i < 1000; i += 2)
        ASSERT_TRUE(m.try_emplace(i, round).has_value());
      for (int i = 0; i < 1000; i += 2)
        EXPECT_EQ(m.erase(i), 1u);
    }
    EXPECT_EQ(m.size(), 500u);
    size_t visited = 0;
    for (auto &kv : m) {
      EXPECT_EQ(kv.first % 2, 1);
      ++visited;
    }
    EXPECT_EQ(visited, 500u);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(HashMap, DuplicateAndAssign) {
  HashMap<int, int> m;
  EXPECT_TRUE(m.try_emplace(1, 10).value().second);
  auto dup = m.try_emplace(1, 99);
  EXPECT_FALSE(dup.value().second);
  EXPECT_EQ(dup.value().first->second, 10);
  auto as = m.insert_or_assign(1, 5);
  EXPECT_FALSE(as.value().second);
  EXPECT_EQ(m.at(1), 5);
  EXPECT_TRUE(m.insert_or_assign(2, 6).value().second);
  EXPECT_TRUE(m.emplace(3, 7).value().second);
  EXPECT_EQ(m.size(), 3u);
  EXPECT_EQ(m.count(3), 1u);
  EXPECT_EQ(m.checked_at(3).value().get(), 7);
  EXPECT_EQ(m.checked_at(9).error(), CommonError::ValueNotFound);
  m.at(3) = 8;
  EXPECT_EQ(m.at(3), 8);
}

TEST(HashMap, StringKeysAndHeterogeneousLookup) {
  TestAllocator alloc;
  {
    HashMap<String, int, Hash<>, EqualTo<>> m(alloc);
    for (int i = 0; i < 100; ++i) {
      String k = MakeStr("key", alloc);
      EXPECT_TRUE(k.append(StringView(i % 2 ? "a" : "b")).has_value());
      String num = MakeStr("", alloc);
      EXPECT_TRUE(num.append(StringView("x")).has_value());
      (void)m.insert_or_assign(Move(k), i);
    }
    EXPECT_EQ(m.size(), 2u);
    auto it = m.find(StringView("keya"));
    ASSERT_TRUE(it != m.end());
    EXPECT_EQ(it->second, 99);
    EXPECT_TRUE(m.contains(StringView("keyb")));
    EXPECT_FALSE(m.contains(StringView("nope")));
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(HashMap, EraseIteratorReturnsNext) {
  HashMap<int, int> m;
  for (int i = 0; i < 50; ++i)
    ASSERT_TRUE(m.try_emplace(i, i).has_value());
  size_t n = 0;
  auto it = m.begin();
  while (it != m.end()) {
    if (it->first % 3 == 0)
      it = m.erase(it);
    else {
      ++it;
    }
    ++n;
  }
  EXPECT_EQ(n, 50u);
  EXPECT_EQ(m.size(), 33u);
  for (int i = 0; i < 50; ++i)
    EXPECT_EQ(m.contains(i), i % 3 != 0);
}

TEST(HashMap, OutOfMemoryLeavesMapUnchanged) {
  TestAllocator alloc;
  HashMap<int, int> m(alloc);
  for (int i = 0; i < 4; ++i)
    ASSERT_TRUE(m.try_emplace(i, i).has_value());
  alloc.fail = true;
  bool failed = false;
  for (int i = 100; i < 200; ++i) {
    auto r = m.try_emplace(i, i);
    if (!r) {
      EXPECT_EQ(r.error(), CommonError::OutOfMemory);
      failed = true;
      break;
    }
  }
  EXPECT_TRUE(failed);
  alloc.fail = false;
  for (int i = 0; i < 4; ++i)
    EXPECT_EQ(m.at(i), i);
  EXPECT_TRUE(m.try_emplace(1000, 1).has_value());
}

TEST(HashMap, CloneMoveSwapClear) {
  TestAllocator alloc;
  {
    HashMap<int, int> a(alloc);
    for (int i = 0; i < 100; ++i)
      ASSERT_TRUE(a.try_emplace(i, -i).has_value());
    auto c = a.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().size(), 100u);
    EXPECT_EQ(c.value().at(42), -42);
    HashMap<int, int> b(alloc);
    ASSERT_TRUE(b.try_emplace(1, 1).has_value());
    a.swap(b);
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(b.size(), 100u);
    HashMap<int, int> moved(Move(b));
    EXPECT_EQ(moved.size(), 100u);
    moved.clear();
    EXPECT_TRUE(moved.empty());
    EXPECT_TRUE(moved.try_emplace(5, 5).has_value());
    EXPECT_TRUE(moved.reserve(500).has_value());
    EXPECT_GE(moved.bucket_count(), 500u);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(HashSet, Basics) {
  TestAllocator alloc;
  {
    HashSet<int> s(alloc);
    for (int i = 0; i < 300; ++i)
      ASSERT_TRUE(s.insert(i % 100).has_value());
    EXPECT_EQ(s.size(), 100u);
    EXPECT_FALSE(s.insert(5).value().second);
    EXPECT_TRUE(s.contains(99));
    EXPECT_FALSE(s.contains(100));
    EXPECT_EQ(s.erase(5), 1u);
    EXPECT_EQ(s.erase(5), 0u);
    EXPECT_EQ(s.count(5), 0u);
    size_t n = 0;
    for (int k : s) {
      EXPECT_NE(k, 5);
      ++n;
    }
    EXPECT_EQ(n, 99u);
    auto c = s.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().size(), 99u);
    auto it = s.find(7);
    ASSERT_TRUE(it != s.end());
    it = s.erase(it);
    EXPECT_FALSE(s.contains(7));
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(HashSet, StringElements) {
  TestAllocator alloc;
  {
    HashSet<String, Hash<>, EqualTo<>> s(alloc);
    ASSERT_TRUE(s.insert(MakeStr("one", alloc)).has_value());
    ASSERT_TRUE(s.insert(MakeStr("two", alloc)).has_value());
    EXPECT_FALSE(s.insert(MakeStr("one", alloc)).value().second);
    EXPECT_TRUE(s.contains(StringView("two")));
  }
  EXPECT_EQ(alloc.live, 0);
}
