#include <vapor/comparators.hpp>
#include <vapor/hash.hpp>

#include <gtest/gtest.h>

using namespace vapor;

namespace {
enum class Color : uint8_t { Red, Green };
}

TEST(HashTest, IntegersSpreadAndAreStable) {
  Hash<int> h;
  EXPECT_EQ(h(42), h(42));
  EXPECT_NE(h(1), h(2));
  // Consecutive keys must not land in consecutive low bits.
  EXPECT_NE(h(1) & 0xFF, h(2) & 0xFF);
  EXPECT_EQ(Hash<char>()('a'), Hash<char>()('a'));
}

TEST(HashTest, EnumBoolAndPointer) {
  EXPECT_NE(Hash<Color>()(Color::Red), Hash<Color>()(Color::Green));
  EXPECT_NE(Hash<bool>()(true), Hash<bool>()(false));
  int a = 0, b = 0;
  EXPECT_EQ(Hash<int *>()(&a), Hash<int *>()(&a));
  EXPECT_NE(Hash<int *>()(&a), Hash<int *>()(&b));
  EXPECT_EQ(Hash<decltype(nullptr)>()(nullptr), 0U);
}

TEST(HashTest, FloatingPointZerosHashAlike) {
  EXPECT_EQ(Hash<double>()(0.0), Hash<double>()(-0.0));
  EXPECT_EQ(Hash<float>()(0.0f), Hash<float>()(-0.0f));
  EXPECT_NE(Hash<double>()(1.0), Hash<double>()(2.0));
}

TEST(HashTest, StringAndStringViewAgree) {
  StringView sv("hello");
  EXPECT_EQ(Hash<StringView>()(sv), Hash<StringView>()(StringView("hello")));
  EXPECT_NE(Hash<StringView>()(sv), Hash<StringView>()(StringView("world")));
  EXPECT_EQ(Hash<StringView>()(StringView("")),
            Hash<StringView>()(StringView()));
}

TEST(HashTest, CombineDependsOnOrder) {
  size_t a = Hash<int>()(1), b = Hash<int>()(2);
  EXPECT_NE(HashCombine(a, b), HashCombine(b, a));
  EXPECT_EQ(HashBytes("abc", 3), HashBytes("abc", 3));
  EXPECT_NE(HashBytes("abc", 3), HashBytes("abd", 3));
}

TEST(ComparatorsTest, TypedAndTransparent) {
  EXPECT_TRUE(Less<int>()(1, 2));
  EXPECT_FALSE(Less<int>()(2, 2));
  EXPECT_TRUE(LessEqual<int>()(2, 2));
  EXPECT_TRUE(Greater<int>()(3, 2));
  EXPECT_TRUE(GreaterEqual<int>()(2, 2));
  EXPECT_TRUE(EqualTo<int>()(2, 2));
  EXPECT_TRUE(NotEqualTo<int>()(1, 2));
  // Transparent: mixed types compare without converting to one type first.
  EXPECT_TRUE(Less<>()(1, 2.5));
  EXPECT_TRUE(EqualTo<>()(StringView("a"), StringView("a")));
}
