#include <vapor/atomic.hpp>

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace vapor;

namespace {
enum class State : uint8_t { Idle, Busy };
struct Pair32 {
  int a;
  int b;
};
} // namespace

TEST(AtomicTest, LoadStoreExchange) {
  Atomic<int> a(5);
  EXPECT_EQ(a.load(), 5);
  a.store(7);
  EXPECT_EQ(a.exchange(9), 7);
  EXPECT_EQ(a.load(MemoryOrder::Acquire), 9);
  a = 11;
  int v = a;
  EXPECT_EQ(v, 11);
}

TEST(AtomicTest, CompareExchangeUpdatesExpectedOnFailure) {
  Atomic<int> a(1);
  int expected = 2;
  EXPECT_FALSE(a.compare_exchange_strong(expected, 3));
  EXPECT_EQ(expected, 1);
  EXPECT_TRUE(a.compare_exchange_strong(expected, 3));
  EXPECT_EQ(a.load(), 3);

  int e2 = 3;
  while (!a.compare_exchange_weak(e2, 4, MemoryOrder::AcqRel,
                                  MemoryOrder::Acquire)) {
  }
  EXPECT_EQ(a.load(), 4);
}

TEST(AtomicTest, IntegerFetchOperationsReturnOldValue) {
  Atomic<uint32_t> a(0b1100);
  EXPECT_EQ(a.fetch_add(1), 12u);
  EXPECT_EQ(a.fetch_sub(1), 13u);
  EXPECT_EQ(a.fetch_and(0b0100), 12u);
  EXPECT_EQ(a.fetch_or(0b0011), 4u);
  EXPECT_EQ(a.fetch_xor(0b0001), 7u);
  EXPECT_EQ(a.load(), 6u);
}

TEST(AtomicTest, OperatorsReturnNewValue) {
  Atomic<int> a(0);
  EXPECT_EQ(++a, 1);
  EXPECT_EQ(a++, 1);
  EXPECT_EQ(--a, 1);
  EXPECT_EQ(a--, 1);
  EXPECT_EQ(a += 5, 5);
  EXPECT_EQ(a -= 2, 3);
  EXPECT_EQ(a |= 4, 7);
  EXPECT_EQ(a &= 5, 5);
  EXPECT_EQ(a ^= 1, 4);
}

TEST(AtomicTest, SmallAndWideIntegers) {
  Atomic<uint8_t> b(250);
  b.fetch_add(10);
  EXPECT_EQ(b.load(), 4);
  Atomic<int64_t> w(int64_t(1) << 40);
  w.fetch_add(1);
  EXPECT_EQ(w.load(), (int64_t(1) << 40) + 1);
}

TEST(AtomicTest, BoolEnumAndTrivialStruct) {
  Atomic<bool> flag(false);
  EXPECT_FALSE(flag.exchange(true));
  EXPECT_TRUE(flag.load());
  Atomic<State> s(State::Idle);
  State exp = State::Idle;
  EXPECT_TRUE(s.compare_exchange_strong(exp, State::Busy));
  EXPECT_EQ(s.load(), State::Busy);
  Atomic<Pair32> p(Pair32{1, 2});
  EXPECT_EQ(p.load().b, 2);
  static_assert(Atomic<int>::is_always_lock_free, "int must be lock-free");
}

TEST(AtomicTest, PointerArithmeticIsScaled) {
  int arr[8] = {};
  Atomic<int *> p(arr);
  EXPECT_EQ(p.fetch_add(3), arr);
  EXPECT_EQ(p.load(), arr + 3);
  ++p;
  EXPECT_EQ(p.load(), arr + 4);
  p -= 2;
  EXPECT_EQ(p.load(), arr + 2);
  EXPECT_EQ(p.fetch_sub(1), arr + 2);
  EXPECT_EQ(p.load(), arr + 1);
}

TEST(AtomicTest, FencesCompile) {
  AtomicThreadFence();
  AtomicSignalFence(MemoryOrder::Acquire);
}

TEST(AtomicTest, ConcurrentIncrementsAreNotLost) {
  Atomic<int> counter(0);
  const int kThreads = 4;
  const int kPerThread = 20000;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t)
    threads.emplace_back([&counter]() {
      for (int i = 0; i < kPerThread; ++i)
        counter.fetch_add(1, MemoryOrder::Relaxed);
    });
  for (size_t i = 0; i < threads.size(); ++i)
    threads[i].join();
  EXPECT_EQ(counter.load(), kThreads * kPerThread);
}

TEST(AtomicTest, ReleaseAcquireHandsOffData) {
  int data = 0;
  Atomic<bool> ready(false);
  std::thread producer([&]() {
    data = 42;
    ready.store(true, MemoryOrder::Release);
  });
  while (!ready.load(MemoryOrder::Acquire)) {
  }
  EXPECT_EQ(data, 42);
  producer.join();
}
