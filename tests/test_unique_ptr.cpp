#include <vapor/unique_ptr.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using vapor::TryAllocateUnique;
using vapor::TryAllocateUniqueArray;
using vapor::UniquePtr;
using vapor_test::TestAllocator;
using vapor_test::Tracked;

namespace {
struct A {
  int a = 1;
  virtual ~A() = default;
};
struct B {
  int b = 2;
};
// B's subobject is not at offset 0, and B has no virtual destructor.
struct Derived : A, B {
  Tracked t{5};
};
} // namespace

TEST(UniquePtrTest, CreatesAndFreesThroughAllocator) {
  TestAllocator alloc;
  Tracked::alive = 0;
  {
    auto p = TryAllocateUnique<Tracked>(alloc, 3);
    ASSERT_TRUE(p);
    EXPECT_EQ(alloc.live, 1);
    EXPECT_EQ((*p)->value, 3);
    EXPECT_EQ((*p).get()->value, 3);
    EXPECT_EQ(Tracked::alive, 1);
  }
  EXPECT_EQ(alloc.live, 0);
  EXPECT_EQ(Tracked::alive, 0);
}

TEST(UniquePtrTest, ReportsOutOfMemory) {
  TestAllocator alloc;
  alloc.fail = true;
  auto p = TryAllocateUnique<int>(alloc, 1);
  ASSERT_FALSE(p);
  EXPECT_EQ(p.error(), vapor::CommonError::OutOfMemory);
}

TEST(UniquePtrTest, MoveResetReleaseSwap) {
  TestAllocator alloc;
  UniquePtr<int> a = TryAllocateUnique<int>(alloc, 1).value();
  UniquePtr<int> b = TryAllocateUnique<int>(alloc, 2).value();
  a.swap(b);
  EXPECT_EQ(*a, 2);
  UniquePtr<int> c(vapor::Move(a));
  EXPECT_FALSE(a);
  EXPECT_TRUE(a == nullptr);
  EXPECT_TRUE(c != nullptr);
  c = vapor::Move(b);
  EXPECT_EQ(alloc.live, 1);
  EXPECT_FALSE(UniquePtr<int>().try_get());

  int *raw = c.release();
  EXPECT_FALSE(c);
  EXPECT_EQ(alloc.live, 1);
  UniquePtr<int> back = UniquePtr<int>::Adopt(alloc, raw);
  back.reset();
  EXPECT_EQ(alloc.live, 0);
}

TEST(UniquePtrTest, ConvertsToBaseAndFreesMostDerived) {
  TestAllocator alloc;
  Tracked::alive = 0;
  {
    UniquePtr<B> base = UniquePtr<B>(
        UniquePtr<Derived>(TryAllocateUnique<Derived>(alloc).value()));
    ASSERT_TRUE(base);
    EXPECT_EQ(base->b, 2);
    EXPECT_EQ(Tracked::alive, 1);
    EXPECT_EQ(alloc.live, 1);
  }
  EXPECT_EQ(Tracked::alive, 0);
  EXPECT_EQ(alloc.live, 0);
}

TEST(UniquePtrTest, ArrayForm) {
  TestAllocator alloc;
  Tracked::alive = 0;
  {
    auto arr = TryAllocateUniqueArray<Tracked>(alloc, 4);
    ASSERT_TRUE(arr);
    EXPECT_EQ((*arr).size(), 4U);
    (*arr)[2].value = 8;
    EXPECT_EQ(Tracked::alive, 4);
  }
  EXPECT_EQ(Tracked::alive, 0);
  EXPECT_EQ(alloc.live, 0);
  EXPECT_TRUE(TryAllocateUniqueArray<int>(alloc, 0));
  EXPECT_FALSE(TryAllocateUniqueArray<int>(alloc, VAPOR_SIZE_MAX));
}
