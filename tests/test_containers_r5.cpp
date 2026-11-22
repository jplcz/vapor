#include <vapor/any.hpp>
#include <vapor/dynamic_bitset.hpp>
#include <vapor/list.hpp>
#include <vapor/multi_map.hpp>
#include <vapor/multi_set.hpp>
#include <vapor/queue.hpp>
#include <vapor/small_vector.hpp>
#include <vapor/stack.hpp>
#include <vapor/string.hpp>
#include <vapor/trie.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using namespace vapor;
using vapor_test::TestAllocator;
typedef vapor_test::TrackedT<62> Tr;

TEST(DynamicBitset, ResizeFillAndQueries) {
  TestAllocator alloc;
  {
    DynamicBitset b(alloc);
    ASSERT_TRUE(b.resize(70).has_value());
    EXPECT_EQ(b.size(), 70u);
    EXPECT_TRUE(b.none());
    b.set(0).set(63).set(64).set(69);
    EXPECT_EQ(b.count(), 4u);
    EXPECT_TRUE(b.test(64));
    EXPECT_EQ(b.checked_test(70).error(), CommonError::OutOfBounds);
    EXPECT_EQ(b.FindFirst(), 0u);
    EXPECT_EQ(b.FindNext(0), 63u);
    EXPECT_EQ(b.FindNext(63), 64u);
    EXPECT_EQ(b.FindNext(69), 70u);
    ASSERT_TRUE(b.resize(200, true).has_value());
    EXPECT_EQ(b.count(), 4u + 130u);
    EXPECT_TRUE(b.test(70));
    EXPECT_TRUE(b.test(199));
    EXPECT_FALSE(b.test(1));
    ASSERT_TRUE(b.resize(65).has_value());
    EXPECT_EQ(b.count(), 3u);
    ASSERT_TRUE(b.resize(130).has_value()); // regrown bits are zero
    EXPECT_EQ(b.count(), 3u);
    b.flip();
    EXPECT_EQ(b.count(), 127u);
    b.reset();
    EXPECT_TRUE(b.none());
    b.set();
    EXPECT_TRUE(b.all());
    ASSERT_TRUE(b.push_back(false).has_value());
    EXPECT_FALSE(b.test(130));
    EXPECT_EQ(b.size(), 131u);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(DynamicBitset, OpsCloneEqualityOom) {
  TestAllocator alloc;
  DynamicBitset a(alloc), b(alloc);
  ASSERT_TRUE(a.resize(100).has_value());
  ASSERT_TRUE(b.resize(100).has_value());
  a.set(1).set(2);
  b.set(2).set(3);
  auto c = a.Clone();
  ASSERT_TRUE(c.has_value());
  EXPECT_TRUE(c.value() == a);
  a |= b;
  EXPECT_EQ(a.count(), 3u);
  a &= b;
  EXPECT_EQ(a.count(), 2u);
  a ^= b;
  EXPECT_TRUE(a.none());
  EXPECT_TRUE(c.value() != b);
  alloc.fail = true;
  EXPECT_EQ(a.resize(100000).error(), CommonError::OutOfMemory);
  EXPECT_EQ(a.size(), 100u);
  alloc.fail = false;
}

TEST(StackQueue, Basics) {
  Stack<int> s;
  for (int i = 0; i < 5; ++i)
    ASSERT_TRUE(s.push(i).has_value());
  EXPECT_EQ(s.top(), 4);
  s.pop();
  EXPECT_EQ(s.top(), 3);
  EXPECT_EQ(s.size(), 4u);
  auto c = s.Clone();
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c.value().top(), 3);
  Stack<int, SmallVector<int, 2>> ss;
  for (int i = 0; i < 5; ++i)
    ASSERT_TRUE(ss.push(i).has_value());
  EXPECT_EQ(ss.top(), 4);

  Queue<int> q;
  for (int i = 0; i < 100; ++i)
    ASSERT_TRUE(q.push(i).has_value());
  EXPECT_EQ(q.front(), 0);
  EXPECT_EQ(q.back(), 99);
  for (int i = 0; i < 50; ++i)
    q.pop();
  EXPECT_EQ(q.front(), 50);
  EXPECT_EQ(q.size(), 50u);
  Queue<int, List<int>> ql;
  ASSERT_TRUE(ql.push(1).has_value());
  ASSERT_TRUE(ql.push(2).has_value());
  ql.pop();
  EXPECT_EQ(ql.front(), 2);
}

TEST(StackQueue, OomAndLeaks) {
  Tr::alive = 0;
  TestAllocator alloc;
  {
    Stack<Tr> s(alloc);
    ASSERT_TRUE(s.emplace(1).has_value());
    alloc.fail = true;
    for (int i = 0; i < 100 && s.push(Tr(i)).has_value(); ++i) {
    }
    alloc.fail = false;
    Queue<Tr> q(alloc);
    ASSERT_TRUE(q.emplace(1).has_value());
  }
  EXPECT_EQ(Tr::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(MultiMap, DuplicatesKeepInsertionOrder) {
  TestAllocator alloc;
  {
    MultiMap<int, int> m(alloc);
    for (int i = 0; i < 300; ++i)
      ASSERT_TRUE(m.emplace(i % 5, i).has_value());
    EXPECT_EQ(m.size(), 300u);
    EXPECT_TRUE(m.CheckInvariants());
    EXPECT_EQ(m.count(2), 60u);
    auto r = m.equal_range(2);
    int prev = -1;
    size_t n = 0;
    for (auto it = r.first; it != r.second; ++it, ++n) {
      EXPECT_EQ(it->first, 2);
      EXPECT_GT(it->second, prev);
      prev = it->second;
    }
    EXPECT_EQ(n, 60u);
    EXPECT_EQ(m.find(2)->second, 2);
    int last = -1;
    for (auto &kv : m) {
      EXPECT_GE(kv.first, last);
      last = kv.first;
    }
    EXPECT_EQ(m.erase(2), 60u);
    EXPECT_EQ(m.erase(2), 0u);
    EXPECT_FALSE(m.contains(2));
    EXPECT_EQ(m.size(), 240u);
    EXPECT_TRUE(m.CheckInvariants());
    auto it = m.erase(m.find(3));
    EXPECT_EQ(it->first, 3);
    auto c = m.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().size(), 239u);
    EXPECT_TRUE(c.value().CheckInvariants());
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(MultiSet, CountAndErase) {
  TestAllocator alloc;
  {
    MultiSet<int> s(alloc);
    for (int i = 0; i < 100; ++i)
      ASSERT_TRUE(s.insert(i % 4).has_value());
    EXPECT_EQ(s.count(1), 25u);
    EXPECT_EQ(*s.lower_bound(2), 2);
    EXPECT_EQ(*s.upper_bound(2), 3);
    EXPECT_EQ(s.erase(1), 25u);
    EXPECT_EQ(s.size(), 75u);
    EXPECT_TRUE(s.CheckInvariants());
    auto c = s.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value().count(3), 25u);
    alloc.fail = true;
    EXPECT_EQ(s.insert(9).error(), CommonError::OutOfMemory);
    EXPECT_EQ(s.size(), 75u);
    alloc.fail = false;
  }
  EXPECT_EQ(alloc.live, 0);
}

struct Big {
  int v[16];
  explicit Big(int x = 0) {
    for (int i = 0; i < 16; ++i)
      v[i] = x;
  }
};
struct MoveOnly {
  int v;
  explicit MoveOnly(int x) : v(x) {}
  MoveOnly(MoveOnly &&o) noexcept : v(o.v) {}
  MoveOnly(const MoveOnly &) = delete;
};

TEST(Any, InlineHeapCastAndTypeChecks) {
  TestAllocator alloc;
  {
    auto a = Any::MakeIn<int>(alloc, 42);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(alloc.live, 0); // stored inline
    EXPECT_TRUE(a.value().is<int>());
    EXPECT_FALSE(a.value().is<long>());
    EXPECT_EQ(*AnyCast<int>(&a.value()), 42);
    EXPECT_TRUE(AnyCast<long>(&a.value()) == nullptr);
    EXPECT_TRUE(AnyCast<int>(static_cast<Any *>(nullptr)) == nullptr);

    auto b = Any::MakeIn<Big>(alloc, 7);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(alloc.live, 1); // too big, heap
    EXPECT_EQ(AnyCast<Big>(&b.value())->v[15], 7);
    Any moved(Move(b.value()));
    EXPECT_FALSE(b.value().has_value());
    EXPECT_EQ(AnyCast<Big>(&moved)->v[0], 7);
    EXPECT_EQ(alloc.live, 1);
    moved.reset();
    EXPECT_EQ(alloc.live, 0);
    EXPECT_TRUE(moved.type_id() == nullptr);
    EXPECT_NE(TypeId<int>(), TypeId<const int>());
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(Any, EmplaceCloneOomAndDestroy) {
  Tr::alive = 0;
  TestAllocator alloc;
  {
    Any a;
    ASSERT_TRUE(a.emplace<Tr>(5).has_value());
    EXPECT_EQ(Tr::alive, 1);
    ASSERT_TRUE(a.emplace<int>(1).has_value()); // replaces and destroys
    EXPECT_EQ(Tr::alive, 0);
    auto c = a.Clone();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(*AnyCast<int>(&c.value()), 1);

    auto m = Any::Make<MoveOnly>(3);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m.value().Clone().error(), CommonError::UnsupportedOperation);

    Any h(Any::MakeIn<Big>(alloc, 1).value());
    auto hc = h.Clone();
    ASSERT_TRUE(hc.has_value());
    EXPECT_EQ(alloc.live, 2);
    alloc.fail = true;
    EXPECT_EQ(h.Clone().error(), CommonError::OutOfMemory);
    alloc.fail = false;
    Any swapped;
    swapped.swap(h);
    EXPECT_TRUE(swapped.is<Big>());
    EXPECT_FALSE(h.has_value());
  }
  EXPECT_EQ(Tr::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(Trie, InsertFindErase) {
  TestAllocator alloc;
  {
    Trie<int> t(alloc);
    ASSERT_TRUE(t.try_emplace("print", 1).value().second);
    ASSERT_TRUE(t.try_emplace("printf", 2).value().second);
    ASSERT_TRUE(t.try_emplace("", 99).value().second);
    EXPECT_FALSE(t.try_emplace("print", 5).value().second);
    EXPECT_EQ(*t.find("print"), 1);
    EXPECT_TRUE(t.find("prin") == nullptr);
    EXPECT_TRUE(t.find("printfx") == nullptr);
    EXPECT_FALSE(t.insert_or_assign("print", 10).value().second);
    EXPECT_EQ(*t.find("print"), 10);
    EXPECT_EQ(t.size(), 3u);
    EXPECT_TRUE(t.HasPrefix("pri"));
    EXPECT_FALSE(t.HasPrefix("x"));
    EXPECT_TRUE(t.erase("print"));
    EXPECT_FALSE(t.erase("print"));
    EXPECT_EQ(*t.find("printf"), 2); // longer key survives
    EXPECT_TRUE(t.erase("printf"));
    EXPECT_FALSE(t.HasPrefix("p")); // pruned
    EXPECT_EQ(t.size(), 1u);
    ASSERT_TRUE(t.try_emplace("again", 3).has_value()); // reuses freed nodes
    EXPECT_EQ(*t.find("again"), 3);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(Trie, LongestPrefixAndForEachOrder) {
  Trie<int> t;
  ASSERT_TRUE(t.try_emplace("/usr", 1).has_value());
  ASSERT_TRUE(t.try_emplace("/usr/bin", 2).has_value());
  ASSERT_TRUE(t.try_emplace("/usr/lib", 3).has_value());
  ASSERT_TRUE(t.try_emplace("/etc", 4).has_value());
  auto m = t.LongestPrefix("/usr/bin/ls");
  ASSERT_TRUE(m.value != nullptr);
  EXPECT_EQ(m.length, 8u);
  EXPECT_EQ(*m.value, 2);
  m = t.LongestPrefix("/usr/share");
  EXPECT_EQ(m.length, 4u);
  EXPECT_TRUE(t.LongestPrefix("/var").value == nullptr);

  String seen;
  ASSERT_TRUE(t.ForEach("/usr", [&seen](StringView key, int &v) {
                 (void)seen.append(key);
                 (void)seen.push_back(static_cast<char>('0' + v));
                 (void)seen.push_back(';');
               }).has_value());
  EXPECT_TRUE(seen == StringView("/usr1;/usr/bin2;/usr/lib3;"));
  size_t n = 0;
  ASSERT_TRUE(t.ForEach("", [&n](StringView, int &) { ++n; }).has_value());
  EXPECT_EQ(n, 4u);
  ASSERT_TRUE(t.ForEach("zzz", [&n](StringView, int &) { ++n; }).has_value());
  EXPECT_EQ(n, 4u);
}

TEST(Trie, BinaryKeysOomAndDestroy) {
  Tr::alive = 0;
  TestAllocator alloc;
  {
    Trie<Tr> t(alloc);
    const char k1[] = {'a', '\0', 'b'};
    ASSERT_TRUE(t.try_emplace(StringView(k1, 3), 1).has_value());
    ASSERT_TRUE(t.try_emplace("a", 2).has_value());
    EXPECT_EQ(t.find(StringView(k1, 3))->value, 1);
    EXPECT_EQ(t.find("a")->value, 2);
    size_t before = t.size();
    alloc.fail = true;
    bool failed = false;
    for (int i = 0; i < 200 && !failed; ++i) {
      String key;
      (void)key.append("longkey");
      (void)key.push_back(static_cast<char>('A' + i % 26));
      failed = !t.try_emplace(StringView(key), i).has_value();
    }
    EXPECT_TRUE(failed);
    EXPECT_GE(t.size(), before);
    alloc.fail = false;
    EXPECT_TRUE(t.find("longkeyZZ") == nullptr);
  }
  EXPECT_EQ(Tr::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}
