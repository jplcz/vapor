#include <vapor/checked_math.hpp>
#include <vapor/chrono.hpp>
#include <vapor/format.hpp>
#include <vapor/path.hpp>
#include <vapor/random.hpp>
#include <vapor/string.hpp>
#include <vapor/string_interner.hpp>
#include <vapor/string_ops.hpp>
#include <vapor/vector.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

using namespace vapor;
using vapor_test::TestAllocator;

TEST(CheckedMath, OverflowDetected) {
  EXPECT_EQ(CheckedAdd<int>(1, 2).value(), 3);
  EXPECT_EQ(CheckedAdd<int>(IntLimits<int>::Max(), 1).error(),
            CommonError::ValueOutOfRange);
  EXPECT_EQ(CheckedSub<int>(IntLimits<int>::Min(), 1).error(),
            CommonError::ValueOutOfRange);
  EXPECT_EQ(CheckedSub<unsigned>(1u, 2u).error(), CommonError::ValueOutOfRange);
  EXPECT_EQ(CheckedMul<int>(100000, 100000).error(),
            CommonError::ValueOutOfRange);
  EXPECT_EQ(CheckedMul<int>(-4, 5).value(), -20);
  EXPECT_EQ(CheckedDiv<int>(1, 0).error(), CommonError::InvalidArgument);
  EXPECT_EQ(CheckedDiv<int>(IntLimits<int>::Min(), -1).error(),
            CommonError::ValueOutOfRange);
  EXPECT_EQ(CheckedRem<int>(7, 3).value(), 1);
  EXPECT_EQ(CheckedNeg<int>(IntLimits<int>::Min()).error(),
            CommonError::ValueOutOfRange);
  EXPECT_EQ(CheckedNeg<int>(5).value(), -5);
}

TEST(CheckedMath, SaturateAndCast) {
  EXPECT_EQ(SaturatingAdd<int>(IntLimits<int>::Max(), 5),
            IntLimits<int>::Max());
  EXPECT_EQ(SaturatingSub<int>(IntLimits<int>::Min(), 5),
            IntLimits<int>::Min());
  EXPECT_EQ(SaturatingSub<unsigned>(1u, 2u), 0u);
  EXPECT_EQ(SaturatingMul<int>(-100000, 100000), IntLimits<int>::Min());
  EXPECT_EQ((CheckedCast<unsigned char, int>(255).value()), 255);
  EXPECT_FALSE((CheckedCast<unsigned char, int>(256).has_value()));
  EXPECT_FALSE((CheckedCast<unsigned, int>(-1).has_value()));
  EXPECT_FALSE((CheckedCast<int, unsigned>(4000000000u).has_value()));
  EXPECT_FALSE(CheckedArraySize(VAPOR_SIZE_MAX / 2, 4).has_value());
  EXPECT_EQ(CheckedArraySize(10, 4).value(), 40u);
}

TEST(StringInterner, DedupAndStableViews) {
  TestAllocator alloc;
  {
    StringInterner in(alloc);
    auto a = in.Intern("position");
    auto b = in.Intern("position");
    auto c = in.Intern("rotation");
    ASSERT_TRUE(a.has_value() && b.has_value() && c.has_value());
    EXPECT_EQ(a.value(), b.value());
    EXPECT_NE(a.value(), c.value());
    StringView first = in.Get(a.value());
    for (int i = 0; i < 2000; ++i) {
      char buf[32];
      int n = snprintf(buf, sizeof buf, "key%d", i);
      ASSERT_TRUE(
          in.Intern(StringView(buf, static_cast<size_t>(n))).has_value());
    }
    EXPECT_TRUE(first == StringView("position"));
    EXPECT_EQ(in.Find("rotation").value(), c.value());
    EXPECT_EQ(in.Find("nope").error(), CommonError::ValueNotFound);
    EXPECT_EQ(in.checked_get(999999).error(), CommonError::OutOfBounds);
    EXPECT_EQ(in.size(), 2002u);
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(StringInterner, OomLeavesUnchanged) {
  TestAllocator alloc;
  StringInterner in(alloc);
  ASSERT_TRUE(in.Intern("a").has_value());
  alloc.fail = true;
  EXPECT_EQ(in.Intern("brand new").error(), CommonError::OutOfMemory);
  EXPECT_EQ(in.size(), 1u);
  alloc.fail = false;
  EXPECT_TRUE(in.Intern("brand new").has_value());
}

TEST(StringOps, ReplaceCaseRepeat) {
  EXPECT_TRUE(ReplaceAll("a-b-c", "-", "::").value() == StringView("a::b::c"));
  EXPECT_TRUE(ReplaceAll("aaa", "aa", "b").value() == StringView("ba"));
  EXPECT_TRUE(ReplaceAll("abc", "", "x").value() == StringView("abc"));
  EXPECT_TRUE(ToLowerCase("HeLLo \xC3\x84").value() ==
              StringView("hello \xC3\x84"));
  EXPECT_TRUE(ToUpperCase("abc1").value() == StringView("ABC1"));
  EXPECT_TRUE(Repeat("ab", 3).value() == StringView("ababab"));
  EXPECT_TRUE(Repeat("ab", 0).value().empty());
  EXPECT_FALSE(Repeat("ab", VAPOR_SIZE_MAX / 3).has_value());
  TestAllocator alloc;
  alloc.fail = true;
  EXPECT_EQ(ToUpperCase("abc", alloc).error(), CommonError::OutOfMemory);
}

TEST(Path, Components) {
  EXPECT_TRUE(PathIsAbsolute("/a"));
  EXPECT_FALSE(PathIsAbsolute("a/b"));
  EXPECT_TRUE(PathBasename("/a/b.txt") == StringView("b.txt"));
  EXPECT_TRUE(PathBasename("a/b/") == StringView(""));
  EXPECT_TRUE(PathDirname("/a/b.txt") == StringView("/a"));
  EXPECT_TRUE(PathDirname("b.txt") == StringView(""));
  EXPECT_TRUE(PathDirname("/b") == StringView("/"));
  EXPECT_TRUE(PathExtension("a/b.tar.gz") == StringView(".gz"));
  EXPECT_TRUE(PathExtension(".bashrc") == StringView(""));
  EXPECT_TRUE(PathExtension("a.b/c") == StringView(""));
  EXPECT_TRUE(PathStem("a/b.tar.gz") == StringView("b.tar"));
}

TEST(Path, JoinAndNormalize) {
  EXPECT_TRUE(PathJoin("a", "b").value() == StringView("a/b"));
  EXPECT_TRUE(PathJoin("a/", "b").value() == StringView("a/b"));
  EXPECT_TRUE(PathJoin("a", "/b").value() == StringView("/b"));
  EXPECT_TRUE(PathJoin("", "b").value() == StringView("b"));
  EXPECT_TRUE(PathNormalize("a/./b//c/../d").value() == StringView("a/b/d"));
  EXPECT_TRUE(PathNormalize("/../a").value() == StringView("/a"));
  EXPECT_TRUE(PathNormalize("../../a").value() == StringView("../../a"));
  EXPECT_TRUE(PathNormalize("a/..").value() == StringView("."));
  EXPECT_TRUE(PathNormalize("").value() == StringView("."));
  EXPECT_TRUE(PathNormalize("/a/b/").value() == StringView("/a/b"));
}

TEST(Chrono, DurationMath) {
  EXPECT_EQ(Duration::FromSeconds(2).milliseconds(), 2000);
  EXPECT_EQ(Duration::FromMinutes(1).seconds(), 60);
  EXPECT_EQ(Duration::FromHours(1).seconds(), 3600);
  EXPECT_EQ((Duration::FromMilliseconds(1500) - Duration::FromSeconds(1))
                .milliseconds(),
            500);
  EXPECT_TRUE(Duration::FromSeconds(1) < Duration::FromSeconds(2));
  EXPECT_EQ((Duration::FromSeconds(3) / 3).seconds(), 1);
  EXPECT_TRUE((Duration::Max() + Duration::FromSeconds(1)) == Duration::Max());
  EXPECT_TRUE((Duration::Min() - Duration::FromSeconds(1)) == Duration::Min());
  EXPECT_TRUE(Duration::FromHours(INT64_C(9000000000)) == Duration::Max());
  EXPECT_DOUBLE_EQ(Duration::FromMilliseconds(500).SecondsAsDouble(), 0.5);
  EXPECT_TRUE(Duration().IsZero());
}

TEST(Chrono, ClocksAreMonotonicAndSleepWorks) {
  TimePoint a = SteadyClock::Now();
  SleepFor(Duration::FromMilliseconds(5));
  TimePoint b = SteadyClock::Now();
  EXPECT_TRUE(b - a >= Duration::FromMilliseconds(4));
  Stopwatch sw;
  SleepFor(Duration::FromMilliseconds(2));
  EXPECT_TRUE(sw.Elapsed() >= Duration::FromMilliseconds(1));
  Duration r = sw.Restart();
  EXPECT_TRUE(r >= Duration::FromMilliseconds(1));
  EXPECT_TRUE(sw.Elapsed() < r + Duration::FromSeconds(5));
  EXPECT_GT(SystemClock::Now().SinceOrigin().seconds(), 1600000000);
}

TEST(Random, DeterministicAndBounded) {
  Random a(42), b(42), c(43);
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(a.Next64(), b.Next64());
  EXPECT_NE(Random(42).Next64(), c.Next64());
  Random r(7);
  int counts[6] = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < 6000; ++i) {
    int v = r.Uniform<int>(1, 6);
    ASSERT_GE(v, 1);
    ASSERT_LE(v, 6);
    ++counts[v - 1];
  }
  for (int i = 0; i < 6; ++i) {
    EXPECT_GT(counts[i], 800);
    EXPECT_LT(counts[i], 1200);
  }
  EXPECT_EQ(r.Uniform<int>(5, 5), 5);
  int lo = r.Uniform<int>(IntLimits<int>::Min(), IntLimits<int>::Max());
  (void)lo;
  for (int i = 0; i < 1000; ++i) {
    double d = r.NextDouble();
    ASSERT_GE(d, 0.0);
    ASSERT_LT(d, 1.0);
    double u = r.Uniform(-2.0, 2.0);
    ASSERT_GE(u, -2.0);
    ASSERT_LT(u, 2.0);
    ASSERT_LT(r.Below(10), 10u);
  }
}

TEST(Random, ShuffleFillAndBool) {
  Random r(1);
  int v[20];
  for (int i = 0; i < 20; ++i)
    v[i] = i;
  r.Shuffle(v, v + 20);
  bool seen[20] = {};
  bool moved = false;
  for (int i = 0; i < 20; ++i) {
    ASSERT_FALSE(seen[v[i]]);
    seen[v[i]] = true;
    moved = moved || v[i] != i;
  }
  EXPECT_TRUE(moved);
  unsigned char buf[37] = {};
  r.Fill(Span<unsigned char>(buf, 37));
  int nonzero = 0;
  for (unsigned char x : buf)
    nonzero += x != 0;
  EXPECT_GT(nonzero, 20);
  EXPECT_FALSE(r.NextBool(0.0));
  EXPECT_TRUE(r.NextBool(1.0));
  Random fc = Random::FromClock();
  (void)fc.Next32();
}

static bool Is(const Expected<String, CommonError> &r, const char *s) {
  return r.has_value() && r.value() == StringView(s);
}

TEST(Format, BasicsAndIndices) {
  EXPECT_TRUE(Is(Format("hello"), "hello"));
  EXPECT_TRUE(Is(Format("{} + {} = {}", 1, 2, 3), "1 + 2 = 3"));
  EXPECT_TRUE(Is(Format("{1} {0} {1}", "a", "b"), "b a b"));
  EXPECT_TRUE(Is(Format("{{}} {}", 5), "{} 5"));
  EXPECT_TRUE(Is(Format("{} {} {} {}", true, 'x', "cs", StringView("sv")),
                 "true x cs sv"));
  EXPECT_TRUE(
      Is(Format("{}", -9223372036854775807LL - 1), "-9223372036854775808"));
  EXPECT_TRUE(
      Is(Format("{}", 18446744073709551615ULL), "18446744073709551615"));
  EXPECT_TRUE(Is(Format("{}", nullptr), "0x0"));
}

TEST(Format, Specs) {
  EXPECT_TRUE(Is(Format("{:5}|{:<5}|{:>5}|{:^5}", 42, 42, 42, 42),
                 "   42|42   |   42| 42  "));
  EXPECT_TRUE(Is(Format("{:*^7}", "ab"), "**ab***"));
  EXPECT_TRUE(Is(Format("{:05}", -42), "-0042"));
  EXPECT_TRUE(Is(Format("{:+}", 5), "+5"));
  EXPECT_TRUE(Is(Format("{:x} {:X} {:#x} {:#b} {:o}", 255, 255, 255, 5, 8),
                 "ff FF 0xff 0b101 10"));
  EXPECT_TRUE(Is(Format("{:.3}", "abcdef"), "abc"));
  EXPECT_TRUE(Is(Format("{:.2f}", 3.14159), "3.14"));
  EXPECT_TRUE(Is(Format("{:08.3f}", 3.14159), "0003.142"));
  EXPECT_TRUE(Is(Format("{:e}", 1500.0), "1.500000e+03"));
  EXPECT_TRUE(Is(Format("{}", 0.5), "0.5"));
  EXPECT_TRUE(Is(Format("{:c}", 65), "A"));
}

TEST(Format, ErrorsAndOom) {
  EXPECT_EQ(Format("{}").error(), CommonError::InvalidArgument);
  EXPECT_EQ(Format("{1}", 1).error(), CommonError::InvalidArgument);
  EXPECT_EQ(Format("{", 1).error(), CommonError::InvalidArgument);
  EXPECT_EQ(Format("}", 1).error(), CommonError::InvalidArgument);
  EXPECT_EQ(Format("{:q}", 1).error(), CommonError::InvalidArgument);
  TestAllocator alloc;
  alloc.fail = true;
  EXPECT_EQ(FormatIn(alloc, "{}", 12345).error(), CommonError::OutOfMemory);
  alloc.fail = false;
  EXPECT_TRUE(FormatIn(alloc, "{}", 12345).has_value());
  StringBuilder sb(alloc);
  ASSERT_TRUE(FormatTo(sb, "{}-{}", 1, 2).has_value());
  ASSERT_TRUE(FormatTo(sb, "!").has_value());
  EXPECT_TRUE(sb.Take() == StringView("1-2!"));
}
