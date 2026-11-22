#include <vapor/scope_guard.hpp>

#include <gtest/gtest.h>

using namespace vapor;

namespace {
int EarlyReturn(int *log, bool bail) {
  VAPOR_DEFER { *log += 1; };
  if (bail)
    return 1;
  *log += 10;
  return 2;
}
} // namespace

TEST(ScopeGuardTest, RunsOnEveryExitPath) {
  int log = 0;
  EXPECT_EQ(EarlyReturn(&log, true), 1);
  EXPECT_EQ(log, 1);
  log = 0;
  EXPECT_EQ(EarlyReturn(&log, false), 2);
  EXPECT_EQ(log, 11);
}

TEST(ScopeGuardTest, DismissCancels) {
  int calls = 0;
  {
    auto g = MakeScopeGuard([&] { ++calls; });
    EXPECT_TRUE(g.IsActive());
    g.Dismiss();
    EXPECT_FALSE(g.IsActive());
  }
  EXPECT_EQ(calls, 0);
}

TEST(ScopeGuardTest, RunIsOnce) {
  int calls = 0;
  {
    auto g = MakeScopeGuard([&] { ++calls; });
    g.Run();
    EXPECT_EQ(calls, 1);
  }
  EXPECT_EQ(calls, 1);
}

TEST(ScopeGuardTest, MoveTransfersDuty) {
  int calls = 0;
  {
    auto a = MakeScopeGuard([&] { ++calls; });
    auto b = Move(a);
    EXPECT_FALSE(a.IsActive());
    EXPECT_TRUE(b.IsActive());
  }
  EXPECT_EQ(calls, 1);
}

TEST(ScopeGuardTest, ReverseOrder) {
  int order[2] = {0, 0};
  int n = 0;
  {
    VAPOR_DEFER { order[0] = ++n; };
    VAPOR_DEFER { order[1] = ++n; };
  }
  EXPECT_EQ(order[1], 1);
  EXPECT_EQ(order[0], 2);
}
