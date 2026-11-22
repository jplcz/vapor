#include <vapor/hash.hpp>
#include <vapor/shared_ptr.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace vapor;
using vapor_test::TestAllocator;
using Obj = vapor_test::TrackedT<41>;

namespace {
struct Base {
  virtual ~Base() {}
  virtual int Id() const { return 1; }
  int base_value = 7;
};
struct Derived : Base {
  int Id() const override { return 2; }
  int extra = 9;
};

struct Node {
  int value;
  WeakPtr<Node> parent;
  SharedPtr<Node> child;
  explicit Node(int v) : value(v) {}
};
} // namespace

TEST(SharedPtrTest, DefaultIsEmpty) {
  SharedPtr<int> p;
  EXPECT_FALSE(p);
  EXPECT_EQ(p.get(), nullptr);
  EXPECT_EQ(p.use_count(), 0u);
  EXPECT_EQ(p.try_get().error(), CommonError::EmptyPointer);
  EXPECT_TRUE(p == nullptr);
}

TEST(SharedPtrTest, MakeCopyAndDestroy) {
  Obj::alive = 0;
  TestAllocator alloc;
  {
    Expected<SharedPtr<Obj>, CommonError> r =
        TryAllocateShared<Obj>(alloc, 5);
    ASSERT_TRUE(r.has_value());
    SharedPtr<Obj> a = Move(r.value());
    EXPECT_EQ(a->value, 5);
    EXPECT_EQ((*a).value, 5);
    EXPECT_EQ(Obj::alive, 1);
    EXPECT_EQ(alloc.live, 1); // object and counts share one block
    EXPECT_TRUE(a.unique());

    SharedPtr<Obj> b = a;
    EXPECT_EQ(a.use_count(), 2u);
    EXPECT_TRUE(a == b);
    b.reset();
    EXPECT_EQ(a.use_count(), 1u);
    EXPECT_EQ(Obj::alive, 1);
  }
  EXPECT_EQ(Obj::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(SharedPtrTest, MoveLeavesSourceEmpty) {
  SharedPtr<int> a = TryMakeShared<int>(3).value();
  SharedPtr<int> b = Move(a);
  EXPECT_FALSE(a);
  EXPECT_EQ(*b, 3);
  EXPECT_EQ(b.use_count(), 1u);
  SharedPtr<int> c;
  c = Move(b);
  EXPECT_EQ(*c, 3);
  c = nullptr;
  EXPECT_FALSE(c);
}

TEST(SharedPtrTest, CopyAssignReleasesOldAndSelfAssignIsSafe) {
  Obj::alive = 0;
  SharedPtr<Obj> a = TryMakeShared<Obj>(1).value();
  SharedPtr<Obj> b = TryMakeShared<Obj>(2).value();
  EXPECT_EQ(Obj::alive, 2);
  b = a;
  EXPECT_EQ(Obj::alive, 1);
  EXPECT_EQ(b->value, 1);
  SharedPtr<Obj> &alias = a;
  a = alias;
  EXPECT_EQ(a.use_count(), 2u);
}

TEST(SharedPtrTest, OutOfMemoryIsReported) {
  Obj::alive = 0;
  TestAllocator alloc;
  alloc.fail = true;
  Expected<SharedPtr<Obj>, CommonError> r = TryAllocateShared<Obj>(alloc, 1);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), CommonError::OutOfMemory);
  EXPECT_EQ(Obj::alive, 0);
}

TEST(SharedPtrTest, ConvertsToBaseAndDestroysDerived) {
  TestAllocator alloc;
  {
    SharedPtr<Derived> d = TryAllocateShared<Derived>(alloc).value();
    SharedPtr<Base> b = d;
    EXPECT_EQ(b->Id(), 2);
    EXPECT_EQ(d.use_count(), 2u);
    SharedPtr<Base> moved = Move(d);
    EXPECT_EQ(moved.use_count(), 2u);
    EXPECT_FALSE(d);
    SharedPtr<Derived> back = StaticPointerCast<Derived>(moved);
    EXPECT_EQ(back->extra, 9);
    EXPECT_EQ(back.use_count(), 3u);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(SharedPtrTest, AliasingConstructorKeepsOwnerAlive) {
  Obj::alive = 0;
  SharedPtr<int> member;
  {
    SharedPtr<Base> owner = TryMakeShared<Base>().value();
    member = SharedPtr<int>(owner, &owner->base_value);
    EXPECT_EQ(owner.use_count(), 2u);
  }
  EXPECT_EQ(member.use_count(), 1u);
  EXPECT_EQ(*member, 7);
}

TEST(WeakPtrTest, LockWhileAliveAndAfterExpiry) {
  Obj::alive = 0;
  WeakPtr<Obj> w;
  EXPECT_TRUE(w.expired());
  EXPECT_FALSE(w.lock());
  {
    SharedPtr<Obj> s = TryMakeShared<Obj>(8).value();
    w = s;
    EXPECT_FALSE(w.expired());
    EXPECT_EQ(w.use_count(), 1u);
    SharedPtr<Obj> locked = w.lock();
    ASSERT_TRUE(locked);
    EXPECT_EQ(locked->value, 8);
    EXPECT_EQ(s.use_count(), 2u);
  }
  EXPECT_EQ(Obj::alive, 0);
  EXPECT_TRUE(w.expired());
  EXPECT_FALSE(w.lock());
}

TEST(WeakPtrTest, BlockOutlivesObjectUntilLastWeakGoes) {
  TestAllocator alloc;
  WeakPtr<int> w;
  {
    SharedPtr<int> s = TryAllocateShared<int>(alloc, 1).value();
    w = s;
  }
  EXPECT_EQ(alloc.live, 1); // counts must stay readable for the weak pointer
  WeakPtr<int> copy = w;
  w.reset();
  EXPECT_EQ(alloc.live, 1);
  copy.reset();
  EXPECT_EQ(alloc.live, 0);
}

TEST(WeakPtrTest, BreaksOwnershipCycle) {
  Obj::alive = 0;
  TestAllocator alloc;
  {
    SharedPtr<Node> parent = TryAllocateShared<Node>(alloc, 1).value();
    SharedPtr<Node> child = TryAllocateShared<Node>(alloc, 2).value();
    parent->child = child;
    child->parent = parent; // weak: does not keep the parent alive
    EXPECT_EQ(parent.use_count(), 1u);
    EXPECT_EQ(child.use_count(), 2u);
    EXPECT_EQ(child->parent.lock()->value, 1);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(SharedPtrTest, HashAndSwap) {
  SharedPtr<int> a = TryMakeShared<int>(1).value();
  SharedPtr<int> b = TryMakeShared<int>(2).value();
  EXPECT_EQ(Hash<SharedPtr<int>>()(a), Hash<int *>()(a.get()));
  int *pa = a.get();
  a.swap(b);
  EXPECT_EQ(b.get(), pa);
  EXPECT_EQ(*a, 2);
}

TEST(SharedPtrTest, CountsStayConsistentAcrossThreads) {
  Obj::alive = 0;
  {
    SharedPtr<Obj> shared = TryMakeShared<Obj>(1).value();
    WeakPtr<Obj> weak = shared;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
      threads.emplace_back([&shared, &weak] {
        for (int i = 0; i < 20000; ++i) {
          SharedPtr<Obj> copy = shared;
          SharedPtr<Obj> locked = weak.lock();
          WeakPtr<Obj> w2 = copy;
          (void)w2;
        }
      });
    for (std::thread &th : threads)
      th.join();
    EXPECT_EQ(shared.use_count(), 1u);
  }
  EXPECT_EQ(Obj::alive, 0);
}

TEST(SharedPtrTest, LastOwnerDroppedOnAnotherThreadDestroysOnce) {
  Obj::alive = 0;
  for (int round = 0; round < 200; ++round) {
    SharedPtr<Obj> p = TryMakeShared<Obj>(round).value();
    SharedPtr<Obj> q = p;
    SharedPtr<Obj> r = p;
    std::thread t1([&q] { q.reset(); });
    std::thread t2([&r] { r.reset(); });
    p.reset();
    t1.join();
    t2.join();
    EXPECT_EQ(Obj::alive, 0);
  }
}
