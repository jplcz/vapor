#include <vapor/functional.hpp>

#include <algorithm>
#include <gtest/gtest.h>

namespace {

class MockAllocator : public vapor::Allocator {
public:
  size_t active_allocations = 0;
  bool should_fail_allocation = false;

  void *Allocate(size_t size, size_t alignment) noexcept override {
    if (should_fail_allocation) {
      return nullptr;
    }
    void *p = nullptr;
    if (alignment < sizeof(void *))
      alignment = sizeof(void *);
    if (::posix_memalign(&p, alignment, size) != 0)
      return nullptr;
    active_allocations++;
    return p;
  }

  void Deallocate(void *ptr, size_t) noexcept override {
    if (ptr) {
      active_allocations--;
      free(ptr);
    }
  }

  void *Reallocate(void *ptr, size_t new_size,
                   size_t /*alignment*/) noexcept override {
    if (new_size == 0) {
      ::free(ptr);
      return nullptr;
    }
    return ::realloc(ptr, new_size);
  }
};

struct Tracker {
  static int instances;
  static int copies;
  static int moves;

  int value;

  explicit Tracker(int v = 0) : value(v) { ++instances; }
  Tracker(const Tracker &other) : value(other.value) {
    ++instances;
    ++copies;
  }
  Tracker(Tracker &&other) noexcept : value(other.value) {
    ++instances;
    ++moves;
  }
  ~Tracker() { --instances; }

  static void ResetCounters() {
    instances = 0;
    copies = 0;
    moves = 0;
  }
};

int Tracker::instances = 0;
int Tracker::copies = 0;
int Tracker::moves = 0;

// Callable that mutates its internal state across calls
struct StatefulFunctor {
  mutable int counter = 0;

  int operator()() const { return ++counter; }
};

// Types designed specifically around the 32-byte boundary
struct Exactly32Bytes {
  char data[32];
  int operator()(int x) const { return x + static_cast<int>(data[0]); }
};

struct Exactly33Bytes {
  char data[33];
  int operator()(int x) const { return x + static_cast<int>(data[0]); }
};

// Non-trivial alignment requirement (e.g., 64-byte aligned SIMD types)
struct alignas(64) HighAlignmentFunctor {
  int val;
  int operator()(int x) const { return val + x; }
};

} // namespace

struct LargeCallable {
  char padding[64];
  int value;

  explicit LargeCallable(int v) : value(v) { std::fill_n(padding, 64, 'A'); }

  int operator()(int x) const { return value + x; }
};

// Free C-style function for raw pointer testing
int AddNumbers(int a, int b) { return a + b; }

class FunctionTest : public ::testing::Test {
protected:
  MockAllocator mock_alloc;

  void SetUp() override { Tracker::ResetCounters(); }
};

TEST_F(FunctionTest, CFunctionPointerSupport) {
  auto fn_res =
      vapor::Function<int(int, int)>::Allocate(&AddNumbers, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  auto &fn = fn_res.value();
  EXPECT_EQ(fn(10, 20), 30);
}

TEST_F(FunctionTest, SmallFunctorSOO) {
  int value = 42;
  auto lambda = [value](int x) { return value + x; };

  // Fits within 32-byte SOO
  static_assert(sizeof(lambda) <= vapor::Function<int(int)>::SOO_SIZE);

  auto fn_res = vapor::Function<int(int)>::Allocate(lambda, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  // Should NOT use dynamic allocation for SOO
  EXPECT_EQ(mock_alloc.active_allocations, 0);
  EXPECT_EQ(fn_res.value()(8), 50);
}

TEST_F(FunctionTest, LargeFunctorHeapAllocation) {
  LargeCallable large(100);
  static_assert(sizeof(LargeCallable) > vapor::Function<int(int)>::SOO_SIZE);

  auto fn_res = vapor::Function<int(int)>::Allocate(large, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  // Must trigger heap allocation
  EXPECT_EQ(mock_alloc.active_allocations, 1);
  EXPECT_EQ(fn_res.value()(5), 105);
}

TEST_F(FunctionTest, AllocationFailureReturnsOutOfMemory) {
  mock_alloc.should_fail_allocation = true;

  LargeCallable large(42);
  auto fn_res = vapor::Function<int(int)>::Allocate(large, mock_alloc);

  ASSERT_FALSE(fn_res.has_value());
  EXPECT_EQ(fn_res.error(), vapor::CommonError::OutOfMemory);
}

TEST_F(FunctionTest, MoveConstructionSOO) {
  {
    Tracker tracker(10);
    auto lambda = [tracker](int x) { return tracker.value + x; };

    auto fn1_res = vapor::Function<int(int)>::Allocate(lambda, mock_alloc);
    ASSERT_TRUE(fn1_res.has_value());

    int initial_instances = Tracker::instances;

    // Move construct fn2 from fn1
    vapor::Function<int(int)> fn2 = std::move(fn1_res.value());

    EXPECT_EQ(fn2(5), 15);
    // Move construction of SOO should transfer object without
    // leaking/duplicating
    EXPECT_EQ(Tracker::instances, initial_instances);
  }
  // All Tracker instances destroyed properly
  EXPECT_EQ(Tracker::instances, 0);
}

TEST_F(FunctionTest, MoveAssignmentHeap) {
  LargeCallable large1(10);
  LargeCallable large2(20);

  auto fn1 = vapor::Function<int(int)>::Allocate(large1, mock_alloc).value();
  auto fn2 = vapor::Function<int(int)>::Allocate(large2, mock_alloc).value();

  EXPECT_EQ(mock_alloc.active_allocations, 2);

  // Moving fn2 into fn1 should release fn1's old buffer
  fn1 = std::move(fn2);

  EXPECT_EQ(mock_alloc.active_allocations, 1);
  EXPECT_EQ(fn1(5), 25);
}

TEST_F(FunctionTest, CloneCFunctionPointer) {
  auto fn =
      vapor::Function<int(int, int)>::Allocate(&AddNumbers, mock_alloc).value();

  auto clone_res = fn.Clone();
  ASSERT_TRUE(clone_res.has_value());

  auto &cloned_fn = clone_res.value();
  EXPECT_EQ(cloned_fn(5, 5), 10);
  EXPECT_EQ(mock_alloc.active_allocations, 0); // No heap used
}

TEST_F(FunctionTest, CloneSOO) {
  Tracker tracker(100);
  auto lambda = [tracker](int x) { return tracker.value + x; };

  auto fn = vapor::Function<int(int)>::Allocate(lambda, mock_alloc).value();

  auto clone_res = fn.Clone();
  ASSERT_TRUE(clone_res.has_value());

  EXPECT_EQ(clone_res.value()(10), 110);
  EXPECT_GT(Tracker::copies, 0); // Must call copy constructor during SOO clone
}

TEST_F(FunctionTest, CloneHeapAllocation) {
  LargeCallable large(50);
  auto fn = vapor::Function<int(int)>::Allocate(large, mock_alloc).value();

  EXPECT_EQ(mock_alloc.active_allocations, 1);

  auto clone_res = fn.Clone();
  ASSERT_TRUE(clone_res.has_value());

  // Deep clone requires a second heap block
  EXPECT_EQ(mock_alloc.active_allocations, 2);
  EXPECT_EQ(clone_res.value()(10), 60);
}

TEST_F(FunctionTest, CloneHeapOOM) {
  LargeCallable large(50);
  auto fn = vapor::Function<int(int)>::Allocate(large, mock_alloc).value();

  // Fail memory allocation for the copy attempt
  mock_alloc.should_fail_allocation = true;

  auto clone_res = fn.Clone();
  ASSERT_FALSE(clone_res.has_value());
  EXPECT_EQ(clone_res.error(), vapor::CommonError::OutOfMemory);
}

TEST_F(FunctionTest, RValueCloneIsMove) {
  Tracker tracker(100);
  auto lambda = [tracker](int x) { return tracker.value + x; };

  auto fn = vapor::Function<int(int)>::Allocate(lambda, mock_alloc).value();
  int moves_before = Tracker::moves;

  // Invoking Clone() on rvalue (std::move(fn))
  auto clone_res = std::move(fn).Clone();
  ASSERT_TRUE(clone_res.has_value());

  EXPECT_GT(Tracker::moves, moves_before); // Should move, not deep-copy
  EXPECT_EQ(clone_res.value()(5), 105);
}

TEST_F(FunctionTest, SOOBoundaryExact32Bytes) {
  // Exactly 32 bytes should still hit the inline SOO buffer path
  static_assert(sizeof(Exactly32Bytes) == 32);
  Exactly32Bytes functor{'A'};

  auto fn_res = vapor::Function<int(int)>::Allocate(functor, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  EXPECT_EQ(mock_alloc.active_allocations, 0); // Must be in SOO
  EXPECT_EQ(fn_res.value()(10), 10 + 'A');
}

TEST_F(FunctionTest, SOOBoundary33BytesForcesHeap) {
  // 33 bytes exceeds 32-byte SOO buffer by 1 byte -> must trigger Heap
  // allocation
  static_assert(sizeof(Exactly33Bytes) >= 33);
  Exactly33Bytes functor{'B'};

  auto fn_res = vapor::Function<int(int)>::Allocate(functor, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  EXPECT_EQ(mock_alloc.active_allocations, 1); // Must trigger heap
  EXPECT_EQ(fn_res.value()(10), 10 + 'B');
}

TEST_F(FunctionTest, HighAlignmentTypeFallsBackToHeap) {
  // Even if small in size, if alignment > alignof(storage), it must bypass SOO
  HighAlignmentFunctor functor{42};

  // Note: sizeof(HighAlignmentFunctor) may be <= 32, but alignment is 64
  auto fn_res = vapor::Function<int(int)>::Allocate(functor, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  if (alignof(HighAlignmentFunctor) > alignof(unsigned long long)) {
    EXPECT_EQ(mock_alloc.active_allocations, 1);
  }
  EXPECT_EQ(fn_res.value()(8), 50);
}

TEST_F(FunctionTest, SelfMoveAssignmentSOO) {
  auto fn_res = vapor::Function<int(int)>::Allocate([](int x) { return x * 2; },
                                                    mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  auto &fn = fn_res.value();

  // Self-move assignment check
  auto &alias = fn; // an alias keeps compilers from flagging the self-move
  fn = std::move(alias);

  // Function must remain intact and safely executable
  EXPECT_EQ(fn(5), 10);
}

TEST_F(FunctionTest, SelfMoveAssignmentHeap) {
  LargeCallable large(100);
  auto fn_res = vapor::Function<int(int)>::Allocate(large, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  auto &fn = fn_res.value();
  size_t alloc_count_before = mock_alloc.active_allocations;

  auto &alias = fn; // an alias keeps compilers from flagging the self-move
  fn = std::move(alias);

  // No double free or premature release of heap allocation
  EXPECT_EQ(mock_alloc.active_allocations, alloc_count_before);
  EXPECT_EQ(fn(5), 105);
}

TEST_F(FunctionTest, DestructorOnDefaultConstructedFunction) {
  // Verifies that destroying a default-constructed (empty) function is a clean
  // no-op
  EXPECT_NO_THROW({ vapor::Function<void()> empty_fn; });
}

TEST_F(FunctionTest, CloneUninitializedFunctionReturnsUnsupported) {
  vapor::Function<void()> empty_fn;

  // Uninitialized function has no vtable -> Clone must return
  // CommonError::UnsupportedOperation
  auto res = empty_fn.Clone();
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), vapor::CommonError::UnsupportedOperation);
}
TEST_F(FunctionTest, VoidReturnTypeAndZeroArguments) {
  bool executed = false;
  auto fn_res = vapor::Function<void()>::Allocate(
      [&executed]() { executed = true; }, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  fn_res.value()();
  EXPECT_TRUE(executed);
}

TEST_F(FunctionTest, PerfectForwardingRValueArguments) {
  // Verifies that non-copyable arguments pass through operator() without
  // copying
  auto fn_res = vapor::Function<int(std::unique_ptr<int> &&)>::Allocate(
      [](std::unique_ptr<int> ptr) { return *ptr + 10; }, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  auto ptr = std::make_unique<int>(5);
  EXPECT_EQ(fn_res.value()(std::move(ptr)), 15);
}

TEST_F(FunctionTest, MutableCallableState) {
  StatefulFunctor stateful;

  auto fn_res = vapor::Function<int()>::Allocate(stateful, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  auto &fn = fn_res.value();
  EXPECT_EQ(fn(), 1);
  EXPECT_EQ(fn(), 2);
  EXPECT_EQ(fn(), 3);
}

TEST_F(FunctionTest, RecursiveInvocation) {
  // Test function calling itself through a captured standard
  // std::function/lambda reference
  std::function<int(int)> recursive_impl;
  recursive_impl = [&recursive_impl](int n) -> int {
    if (n <= 1)
      return 1;
    return n * recursive_impl(n - 1);
  };

  auto fn_res = vapor::Function<int(int)>::Allocate(recursive_impl, mock_alloc);
  ASSERT_TRUE(fn_res.has_value());

  EXPECT_EQ(fn_res.value()(5), 120);
}
