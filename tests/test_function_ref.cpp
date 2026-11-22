#include <vapor/function_ref.hpp>

#include <gtest/gtest.h>
#include <type_traits>

using vapor::FunctionRef;

namespace {
int Twice(int x) { return 2 * x; }
long Widen(long x) { return x + 1; }

struct Counter {
  int calls = 0;
  int operator()(int x) {
    ++calls;
    return x;
  }
};
struct ConstFn {
  int operator()(int x) const { return x + 100; }
};

int Apply(FunctionRef<int(int)> f, int v) { return f(v); }
} // namespace

TEST(FunctionRefTest, FunctionAndPointer) {
  FunctionRef<int(int)> a(Twice);
  EXPECT_EQ(a(4), 8);
  FunctionRef<int(int)> b(&Twice);
  EXPECT_EQ(b(5), 10);
  FunctionRef<int(int)> c(Widen); // int converts to long and back
  EXPECT_EQ(c(1), 2);
}

TEST(FunctionRefTest, LvalueFunctorsAreReferencedNotCopied) {
  Counter counter;
  FunctionRef<int(int)> r(counter);
  EXPECT_EQ(r(3), 3);
  EXPECT_EQ(Apply(counter, 4), 4);
  EXPECT_EQ(counter.calls, 2);

  const ConstFn cf;
  EXPECT_EQ(Apply(cf, 1), 101);

  int base = 10;
  auto lambda = [&base](int x) { return x + base; };
  EXPECT_EQ(Apply(lambda, 1), 11);
  base = 20;
  EXPECT_EQ(r.operator bool(), true);
  FunctionRef<int(int)> l(lambda);
  EXPECT_EQ(l(1), 21);
}

TEST(FunctionRefTest, EmptyAndCopy) {
  FunctionRef<int(int)> e;
  EXPECT_FALSE(e);
  FunctionRef<int(int)> n(nullptr);
  EXPECT_FALSE(n);
  int (*null_fp)(int) = nullptr;
  FunctionRef<int(int)> f(null_fp);
  EXPECT_FALSE(f);
  FunctionRef<int(int)> a(Twice);
  FunctionRef<int(int)> b(a);
  EXPECT_EQ(b(2), 4);
  EXPECT_TRUE(std::is_trivially_copyable<FunctionRef<int(int)>>::value);
}

TEST(FunctionRefTest, VoidAndReferenceArguments) {
  int total = 0;
  auto add = [&total](int &x) { total += x; };
  FunctionRef<void(int &)> r(add);
  int v = 5;
  r(v);
  EXPECT_EQ(total, 5);
}

TEST(FunctionRefTest, TemporariesAreRejected) {
  auto lam = [](int x) { return x; };
  static_assert(
      std::is_constructible<FunctionRef<int(int)>, decltype(lam) &>::value, "");
  static_assert(
      !std::is_constructible<FunctionRef<int(int)>, decltype(lam)>::value, "");
  static_assert(
      !std::is_constructible<FunctionRef<int(int)>, ConstFn>::value, "");
}

TEST(FunctionRefDeathTest, CallingEmptyAborts) {
  FunctionRef<int(int)> e;
  EXPECT_DEATH(e(1), "");
}
