#include <vapor/expected.hpp>
#include <vapor/span.hpp>
#include <vapor/string.hpp>

#include <gtest/gtest.h>

namespace {
struct CopyCounter {
  int copies = 0;
  CopyCounter() = default;
  CopyCounter(const CopyCounter &o) : copies(o.copies + 1) {}
  CopyCounter(CopyCounter &&o) noexcept : copies(o.copies) {}
};
using CopyExp = vapor::Expected<CopyCounter, int>;
} // namespace

TEST(HardeningTest, StringComparisonHonoursEmbeddedNul) {
  vapor::String a, b;
  ASSERT_TRUE(a.append(vapor::StringView("a\0b", 3)));
  ASSERT_TRUE(b.append(vapor::StringView("a\0c", 3)));
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(a != "a");
  EXPECT_TRUE(a != vapor::StringView("a"));
}

TEST(HardeningTest, StringAppendOwnContentSurvivesReallocation) {
  vapor::String s;
  ASSERT_TRUE(s.append(vapor::StringView("abcdefgh")));
  ASSERT_EQ(s.capacity(), 8U);
  ASSERT_TRUE(s.append(vapor::StringView(s.data(), s.size())));
  EXPECT_TRUE(s == "abcdefghabcdefgh");
}

TEST(HardeningTest, SpanConvertsToConstSpan) {
  int arr[] = {1, 2, 3};
  vapor::Span<int> mut(arr);
  vapor::Span<const int> ro = mut;
  EXPECT_EQ(ro.size(), 3U);
  EXPECT_EQ(ro[1], 2);
}

TEST(HardeningTest, ExpectedRvalueOperationsMoveInsteadOfCopy) {
  CopyExp a{CopyCounter()};
  CopyExp b(static_cast<CopyExp &&>(a));
  EXPECT_EQ(b.value().copies, 0);
  EXPECT_EQ(static_cast<CopyExp &&>(b).value_or(CopyCounter()).copies, 0);

  CopyExp err{vapor::MakeUnexpected(1)};
  EXPECT_EQ(err.value_or(CopyCounter()).copies, 0);
}
