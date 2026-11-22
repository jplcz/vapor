#include <vapor/string_util.hpp>

#include <gtest/gtest.h>
#include <stdint.h>

using namespace vapor;

TEST(StringUtilTest, Trim) {
  EXPECT_EQ(Trim("  hi \n"), StringView("hi"));
  EXPECT_EQ(TrimLeft("  hi "), StringView("hi "));
  EXPECT_EQ(TrimRight("  hi "), StringView("  hi"));
  EXPECT_TRUE(Trim("   \t").empty());
  EXPECT_TRUE(Trim("").empty());
}

TEST(StringUtilTest, ConsumePrefixSuffix) {
  StringView sv("--name=bob");
  EXPECT_TRUE(ConsumePrefix(sv, "--"));
  EXPECT_EQ(sv, StringView("name=bob"));
  EXPECT_FALSE(ConsumePrefix(sv, "--"));
  EXPECT_TRUE(ConsumeSuffix(sv, "bob"));
  EXPECT_EQ(sv, StringView("name="));
}

TEST(StringUtilTest, FindAndContains) {
  StringView t("hello world");
  EXPECT_EQ(Find(t, "world"), 6u);
  EXPECT_EQ(Find(t, 'o'), 4u);
  EXPECT_EQ(Find(t, 'o', 5), 7u);
  EXPECT_EQ(Find(t, "xyz"), StringView::npos);
  EXPECT_EQ(Find(t, "", 3), 3u);
  EXPECT_EQ(Find(t, "d", 99), StringView::npos);
  EXPECT_TRUE(Contains(t, "lo w"));
  EXPECT_FALSE(Contains(t, 'z'));
}

TEST(StringUtilTest, AsciiHelpers) {
  EXPECT_TRUE(IsAsciiSpace('\t'));
  EXPECT_TRUE(IsAsciiDigit('7'));
  EXPECT_TRUE(IsAsciiAlpha('q'));
  EXPECT_EQ(AsciiToLower('Q'), 'q');
  EXPECT_EQ(AsciiToUpper('q'), 'Q');
  EXPECT_EQ(AsciiToUpper('1'), '1');
  EXPECT_TRUE(EqualsIgnoreCase("HeLLo", "hello"));
  EXPECT_FALSE(EqualsIgnoreCase("hello", "hell"));
}

namespace {
size_t CollectPieces(StringView text, char delim, StringView *out,
                     size_t cap) {
  size_t n = 0;
  for (StringView part : Split(text, delim))
    if (n < cap)
      out[n++] = part;
  return n;
}
} // namespace

TEST(StringUtilTest, SplitByChar) {
  StringView parts[8];
  size_t n = CollectPieces("a,b,,c", ',', parts, 8);
  ASSERT_EQ(n, 4u);
  EXPECT_EQ(parts[0], StringView("a"));
  EXPECT_EQ(parts[1], StringView("b"));
  EXPECT_TRUE(parts[2].empty());
  EXPECT_EQ(parts[3], StringView("c"));

  EXPECT_EQ(CollectPieces("", ',', parts, 8), 1u);
  EXPECT_EQ(CollectPieces(",", ',', parts, 8), 2u);
  EXPECT_EQ(CollectPieces("abc", ',', parts, 8), 1u);
  EXPECT_EQ(parts[0], StringView("abc"));
  EXPECT_EQ(CollectPieces("a,", ',', parts, 8), 2u);
  EXPECT_TRUE(parts[1].empty());
}

TEST(StringUtilTest, SplitByString) {
  size_t n = 0;
  StringView got[4];
  for (StringView p : Split("one::two::three", "::"))
    got[n++] = p;
  ASSERT_EQ(n, 3u);
  EXPECT_EQ(got[1], StringView("two"));

  EXPECT_EQ(Split("whole text", "").count(), 1u);
  EXPECT_EQ(Split("a--b", "--").count(), 2u);
  EXPECT_EQ(Split("xx", "x").count(), 3u);
}

TEST(StringUtilTest, SplitOnce) {
  Expected<Pair<StringView, StringView>, CommonError> kv =
      SplitOnce("key=a=b", '=');
  ASSERT_TRUE(kv.has_value());
  EXPECT_EQ(kv.value().first, StringView("key"));
  EXPECT_EQ(kv.value().second, StringView("a=b"));

  EXPECT_EQ(SplitOnce("novalue", '=').error(), CommonError::ValueNotFound);
  EXPECT_EQ(SplitOnce("a::b", "::").value().second, StringView("b"));
  EXPECT_FALSE(SplitOnce("abc", "").has_value());
}

TEST(StringUtilTest, ParseIntBasics) {
  EXPECT_EQ(ParseInt<int>("42").value(), 42);
  EXPECT_EQ(ParseInt<int>("-42").value(), -42);
  EXPECT_EQ(ParseInt<int>("+7").value(), 7);
  EXPECT_EQ(ParseInt<uint8_t>("255").value(), 255);
  EXPECT_EQ(ParseInt<int64_t>("-9223372036854775808").value(), INT64_MIN);
  EXPECT_EQ(ParseInt<uint64_t>("18446744073709551615").value(), UINT64_MAX);
  EXPECT_EQ(ParseInt<int8_t>("-128").value(), -128);
}

TEST(StringUtilTest, ParseIntErrors) {
  EXPECT_EQ(ParseInt<int>("").error(), CommonError::InvalidArgument);
  EXPECT_EQ(ParseInt<int>("-").error(), CommonError::InvalidArgument);
  EXPECT_EQ(ParseInt<int>("12a").error(), CommonError::InvalidArgument);
  EXPECT_EQ(ParseInt<int>(" 1").error(), CommonError::InvalidArgument);
  EXPECT_EQ(ParseInt<unsigned>("-1").error(), CommonError::InvalidArgument);
  EXPECT_EQ(ParseInt<int>("1", 1).error(), CommonError::InvalidArgument);
  EXPECT_EQ(ParseInt<int>("1", 37).error(), CommonError::InvalidArgument);

  EXPECT_EQ(ParseInt<uint8_t>("256").error(), CommonError::ValueOutOfRange);
  EXPECT_EQ(ParseInt<int8_t>("128").error(), CommonError::ValueOutOfRange);
  EXPECT_EQ(ParseInt<int8_t>("-129").error(), CommonError::ValueOutOfRange);
  EXPECT_EQ(ParseInt<int64_t>("9223372036854775808").error(),
            CommonError::ValueOutOfRange);
  EXPECT_EQ(ParseInt<uint64_t>("18446744073709551616").error(),
            CommonError::ValueOutOfRange);
  EXPECT_EQ(ParseInt<int>("99999999999999999999999").error(),
            CommonError::ValueOutOfRange);
  // A bad character after an overflow is still reported as invalid text.
  EXPECT_EQ(ParseInt<uint8_t>("9999x").error(), CommonError::InvalidArgument);
}

TEST(StringUtilTest, ParseIntBases) {
  EXPECT_EQ(ParseInt<int>("ff", 16).value(), 255);
  EXPECT_EQ(ParseInt<int>("0xFF", 16).value(), 255);
  EXPECT_EQ(ParseInt<int>("0xff", 0).value(), 255);
  EXPECT_EQ(ParseInt<int>("0b101", 0).value(), 5);
  EXPECT_EQ(ParseInt<int>("101", 2).value(), 5);
  EXPECT_EQ(ParseInt<int>("017", 0).value(), 15);
  EXPECT_EQ(ParseInt<int>("0", 0).value(), 0);
  EXPECT_EQ(ParseInt<int>("-0x10", 0).value(), -16);
  EXPECT_EQ(ParseInt<int>("zz", 36).value(), 35 * 36 + 35);
  EXPECT_EQ(ParseInt<int>("19", 8).error(), CommonError::InvalidArgument);
  EXPECT_EQ(ParseInt<int>("0x", 16).error(), CommonError::InvalidArgument);
}

TEST(StringUtilTest, ParseBoolAndDouble) {
  EXPECT_TRUE(ParseBool("TRUE").value());
  EXPECT_TRUE(ParseBool("1").value());
  EXPECT_FALSE(ParseBool("False").value());
  EXPECT_FALSE(ParseBool("0").value());
  EXPECT_FALSE(ParseBool("yes").has_value());

  EXPECT_DOUBLE_EQ(ParseDouble("3.5").value(), 3.5);
  EXPECT_DOUBLE_EQ(ParseDouble("-1e3").value(), -1000.0);
  EXPECT_FALSE(ParseDouble("").has_value());
  EXPECT_FALSE(ParseDouble("1.5x").has_value());
  EXPECT_FALSE(ParseDouble(" 1").has_value());
  EXPECT_EQ(ParseDouble("1e999").error(), CommonError::ValueOutOfRange);
}
