#include <vapor/string_builder.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>
#include <stdint.h>

using namespace vapor;

TEST(StringBuilderTest, AppendsTextAndChars) {
  StringBuilder sb;
  EXPECT_TRUE(sb.empty());
  ASSERT_TRUE(sb.Append("ab").has_value());
  ASSERT_TRUE(sb.Append(StringView("cd")).has_value());
  ASSERT_TRUE(sb.Append('e').has_value());
  String s;
  ASSERT_TRUE(s.append("fg").has_value());
  ASSERT_TRUE(sb.Append(s).has_value());
  EXPECT_EQ(sb.view(), StringView("abcdefg"));
  EXPECT_STREQ(sb.c_str(), "abcdefg");
  EXPECT_EQ(sb.size(), 7u);
  ASSERT_TRUE(sb.Append(static_cast<const char *>(nullptr)).has_value());
  EXPECT_EQ(sb.size(), 7u);
}

TEST(StringBuilderTest, AppendsIntegers) {
  StringBuilder sb;
  ASSERT_TRUE(sb.Append(0).has_value());
  ASSERT_TRUE(sb.Append(' ').has_value());
  ASSERT_TRUE(sb.Append(-42).has_value());
  ASSERT_TRUE(sb.Append(' ').has_value());
  ASSERT_TRUE(sb.Append(static_cast<uint64_t>(18446744073709551615ULL))
                  .has_value());
  ASSERT_TRUE(sb.Append(' ').has_value());
  ASSERT_TRUE(sb.Append(static_cast<int64_t>(INT64_MIN)).has_value());
  ASSERT_TRUE(sb.Append(' ').has_value());
  ASSERT_TRUE(sb.Append(static_cast<int8_t>(-128)).has_value());
  EXPECT_EQ(sb.view(), StringView("0 -42 18446744073709551615 "
                                  "-9223372036854775808 -128"));
}

TEST(StringBuilderTest, BoolAndFloat) {
  StringBuilder sb;
  ASSERT_TRUE(sb.AppendAll(true, '/', false, '/', 1.5, '/', 0.1f).has_value());
  EXPECT_EQ(sb.view(), StringView("true/false/1.5/0.1"));
  sb.clear();
  ASSERT_TRUE(sb.Append(3.14159265, 3).has_value());
  EXPECT_EQ(sb.view(), StringView("3.14"));
}

TEST(StringBuilderTest, BasesAndHex) {
  StringBuilder sb;
  ASSERT_TRUE(sb.AppendInt(255, 2).has_value());
  ASSERT_TRUE(sb.Append(' ').has_value());
  ASSERT_TRUE(sb.AppendInt(-255, 16, true).has_value());
  ASSERT_TRUE(sb.Append(' ').has_value());
  ASSERT_TRUE(sb.AppendInt(35, 36).has_value());
  EXPECT_EQ(sb.view(), StringView("11111111 -FF z"));
  EXPECT_EQ(sb.AppendInt(1, 1).error(), CommonError::InvalidArgument);
  EXPECT_EQ(sb.AppendInt(1, 37).error(), CommonError::InvalidArgument);

  sb.clear();
  ASSERT_TRUE(sb.AppendHex(0xBEEFu, 8, true).has_value());
  EXPECT_EQ(sb.view(), StringView("0000BEEF"));
  sb.clear();
  ASSERT_TRUE(sb.AppendHex(static_cast<int8_t>(-1)).has_value());
  EXPECT_EQ(sb.view(), StringView("ff"));
  sb.clear();
  ASSERT_TRUE(sb.AppendHex(0).has_value());
  EXPECT_EQ(sb.view(), StringView("0"));
}

TEST(StringBuilderTest, RepeatJoinAndAll) {
  StringBuilder sb;
  ASSERT_TRUE(sb.AppendRepeat('-', 3).has_value());
  ASSERT_TRUE(sb.AppendRepeat('x', 0).has_value());
  EXPECT_EQ(sb.view(), StringView("---"));

  sb.clear();
  const char *words[] = {"a", "b", "c"};
  ASSERT_TRUE(sb.AppendJoin(words, ", ").has_value());
  EXPECT_EQ(sb.view(), StringView("a, b, c"));

  sb.clear();
  int nums[] = {1, 2, 3};
  ASSERT_TRUE(sb.AppendJoin(nums, "+").has_value());
  EXPECT_EQ(sb.view(), StringView("1+2+3"));

  sb.clear();
  ASSERT_TRUE(sb.AppendAll("user ", 42, " ok=", true, '\n').has_value());
  EXPECT_EQ(sb.view(), StringView("user 42 ok=true\n"));
  EXPECT_TRUE(sb.AppendAll().has_value());
}

TEST(StringBuilderTest, SelfAppendAndTake) {
  StringBuilder sb;
  ASSERT_TRUE(sb.Append("ab").has_value());
  ASSERT_TRUE(sb.Append(sb.view()).has_value());
  EXPECT_EQ(sb.view(), StringView("abab"));

  String out = sb.Take();
  EXPECT_STREQ(out.c_str(), "abab");
  EXPECT_TRUE(sb.empty());
  ASSERT_TRUE(sb.Append("again").has_value());
  EXPECT_STREQ(sb.c_str(), "again");
}

TEST(StringBuilderTest, ReportsOutOfMemoryAndKeepsPrefix) {
  vapor_test::TestAllocator alloc;
  {
    StringBuilder sb(alloc);
    ASSERT_TRUE(sb.Append("keep").has_value());
    alloc.fail = true;
    Expected<void, CommonError> r =
        sb.AppendAll("a long text that forces the buffer to grow", "more");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), CommonError::OutOfMemory);
    EXPECT_EQ(sb.view(), StringView("keep"));
    alloc.fail = false;
    ASSERT_TRUE(sb.Append("!").has_value());
    EXPECT_EQ(sb.view(), StringView("keep!"));
  }
  EXPECT_EQ(alloc.live, 0);
}
