#include <vapor/hash.hpp>
#include <vapor/small_string.hpp>

#include <gtest/gtest.h>

using namespace vapor;

TEST(SmallStringTest, StartsEmptyAndNulTerminated) {
  SmallString<8> s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0u);
  EXPECT_EQ(SmallString<8>::capacity(), 8u);
  EXPECT_STREQ(s.c_str(), "");
}

TEST(SmallStringTest, AppendPushBackAndCapacity) {
  SmallString<5> s;
  ASSERT_TRUE(s.append("abc").has_value());
  ASSERT_TRUE(s.push_back('d').has_value());
  ASSERT_TRUE(s.push_back('e').has_value());
  EXPECT_STREQ(s.c_str(), "abcde");

  Expected<void, CommonError> r = s.push_back('f');
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), CommonError::CapacityExceeded);
  r = s.append("x");
  EXPECT_FALSE(r.has_value());
  EXPECT_STREQ(s.c_str(), "abcde"); // unchanged on failure

  s.pop_back();
  EXPECT_STREQ(s.c_str(), "abcd");
}

TEST(SmallStringTest, FromAndAssign) {
  Expected<SmallString<4>, CommonError> ok = SmallString<4>::From("abcd");
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value(), "abcd");
  Expected<SmallString<4>, CommonError> bad = SmallString<4>::From("abcde");
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error(), CommonError::CapacityExceeded);

  SmallString<4> s;
  EXPECT_FALSE(s.assign("too long").has_value());
  ASSERT_TRUE(s.assign("hi").has_value());
  EXPECT_EQ(s.size(), 2u);
}

TEST(SmallStringTest, AppendSelfAliasing) {
  SmallString<16> s;
  ASSERT_TRUE(s.append("ab").has_value());
  ASSERT_TRUE(s.append(s.view()).has_value());
  EXPECT_STREQ(s.c_str(), "abab");
}

TEST(SmallStringTest, EraseInsertResize) {
  SmallString<16> s;
  ASSERT_TRUE(s.assign("hello world").has_value());
  ASSERT_TRUE(s.erase(5, 100).has_value());
  EXPECT_STREQ(s.c_str(), "hello");
  EXPECT_FALSE(s.erase(6, 1).has_value());

  ASSERT_TRUE(s.insert(0, ">> ").has_value());
  EXPECT_STREQ(s.c_str(), ">> hello");
  EXPECT_FALSE(s.insert(99, "x").has_value());
  EXPECT_EQ(s.insert(0, "0123456789").error(), CommonError::CapacityExceeded);

  ASSERT_TRUE(s.resize(3).has_value());
  EXPECT_STREQ(s.c_str(), ">> ");
  ASSERT_TRUE(s.resize(5, '.').has_value());
  EXPECT_STREQ(s.c_str(), ">> ..");
  EXPECT_FALSE(s.resize(17).has_value());
}

TEST(SmallStringTest, AccessAndIteration) {
  SmallString<8> s;
  ASSERT_TRUE(s.assign("abc").has_value());
  EXPECT_EQ(s[1], 'b');
  EXPECT_EQ(s.at(2), 'c');
  EXPECT_EQ(s.front(), 'a');
  EXPECT_EQ(s.back(), 'c');
  EXPECT_FALSE(s.checked_at(3).has_value());
  EXPECT_EQ(s.checked_at(0).value().get(), 'a');
  s[0] = 'X';
  EXPECT_STREQ(s.c_str(), "Xbc");
  int n = 0;
  for (char c : s) {
    (void)c;
    ++n;
  }
  EXPECT_EQ(n, 3);
}

TEST(SmallStringTest, CopyIsIndependentAndCannotFail) {
  SmallString<8> a;
  ASSERT_TRUE(a.assign("one").has_value());
  SmallString<8> b(a);
  ASSERT_TRUE(b.push_back('!').has_value());
  EXPECT_STREQ(a.c_str(), "one");
  EXPECT_STREQ(b.c_str(), "one!");
  a.swap(b);
  EXPECT_STREQ(a.c_str(), "one!");
}

TEST(SmallStringTest, ComparisonsAndConversions) {
  SmallString<8> a;
  SmallString<16> b;
  ASSERT_TRUE(a.assign("abc").has_value());
  ASSERT_TRUE(b.assign("abd").has_value());
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a == "abc");
  EXPECT_TRUE(a == StringView("abc"));
  EXPECT_TRUE(StringView("abc") == a);
  EXPECT_TRUE(a != "abd");
  StringView sv = a;
  EXPECT_EQ(sv.size(), 3u);
}

TEST(SmallStringTest, HashMatchesStringView) {
  SmallString<8> a;
  ASSERT_TRUE(a.assign("key").has_value());
  EXPECT_EQ(Hash<SmallString<8>>()(a), Hash<StringView>()("key"));
}
