#include <vapor/bitset.hpp>

#include <gtest/gtest.h>

using vapor::Bitset;

TEST(BitsetTest, SetTestResetFlipCount) {
  Bitset<100> b;
  EXPECT_TRUE(b.none());
  b.set(0).set(63).set(64).set(99);
  EXPECT_EQ(b.count(), 4U);
  EXPECT_TRUE(b.test(63));
  EXPECT_TRUE(b[64]);
  EXPECT_FALSE(b.test(65));
  b.reset(63);
  EXPECT_FALSE(b.test(63));
  b.flip(5);
  EXPECT_TRUE(b.test(5));
  b[7] = true;
  EXPECT_TRUE(b.test(7));
  b[7] = false;
  EXPECT_FALSE(b.test(7));
  EXPECT_EQ(b.size(), 100U);
  EXPECT_TRUE(b.any());
}

TEST(BitsetTest, WholeSetOperationsKeepTailClean) {
  Bitset<70> b;
  b.set();
  EXPECT_EQ(b.count(), 70U);
  EXPECT_TRUE(b.all());
  b.flip();
  EXPECT_TRUE(b.none());
  EXPECT_EQ((~Bitset<70>()).count(), 70U);
  Bitset<70> a;
  a.set(1).set(69);
  EXPECT_EQ(((~a) & a).count(), 0U);
  EXPECT_EQ((a | ~a).count(), 70U);
  EXPECT_EQ((a ^ a).count(), 0U);
  EXPECT_TRUE(a == a);
  EXPECT_TRUE(a != ~a);
}

TEST(BitsetTest, Shifts) {
  Bitset<130> b;
  b.set(0).set(1).set(129);
  Bitset<130> l = b << 65;
  EXPECT_TRUE(l.test(65));
  EXPECT_TRUE(l.test(66));
  EXPECT_EQ(l.count(), 2U); // bit 129 shifted out
  Bitset<130> r = b >> 1;
  EXPECT_TRUE(r.test(0));
  EXPECT_TRUE(r.test(128));
  EXPECT_EQ(r.count(), 2U); // old bit 0 is lost, old bit 1 becomes bit 0
  EXPECT_TRUE((b << 130).none());
  EXPECT_TRUE((b >> 200).none());
  Bitset<130> s = b << 64;
  EXPECT_TRUE(s.test(64));
  EXPECT_TRUE(s.test(65));
  EXPECT_EQ((b << 3 >> 3).count(), 2U); // bit 129 is lost on the way
}

TEST(BitsetTest, FindFirstAndNext) {
  Bitset<200> b;
  EXPECT_EQ(b.FindFirst(), 200U);
  b.set(3).set(64).set(130).set(199);
  size_t seen[4];
  size_t n = 0;
  for (size_t i = b.FindFirst(); i != 200; i = b.FindNext(i))
    seen[n++] = i;
  ASSERT_EQ(n, 4U);
  EXPECT_EQ(seen[0], 3U);
  EXPECT_EQ(seen[1], 64U);
  EXPECT_EQ(seen[2], 130U);
  EXPECT_EQ(seen[3], 199U);
}

TEST(BitsetTest, ConvertFromAndToInteger) {
  Bitset<8> b(0x1FFULL); // bit 8 is dropped
  EXPECT_EQ(b.count(), 8U);
  EXPECT_EQ(b.ToUint64(), 0xFFU);
  Bitset<100> big(5);
  EXPECT_EQ(big.ToUint64(), 5U);
}

TEST(BitsetTest, CheckedAccessAndZeroSize) {
  Bitset<4> b;
  b.set(2);
  EXPECT_TRUE(b.checked_test(2).value());
  auto bad = b.checked_test(4);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error(), vapor::CommonError::OutOfBounds);

  Bitset<0> z;
  EXPECT_TRUE(z.none());
  EXPECT_TRUE(z.all());
  EXPECT_EQ(z.FindFirst(), 0U);
  EXPECT_FALSE(z.checked_test(0));
}

TEST(BitsetDeathTest, OutOfRangeAborts) {
  Bitset<4> b;
  EXPECT_DEATH((void)b.test(4), "");
  EXPECT_DEATH(b.set(9), "");
}

TEST(BitsetTest, Swap) {
  Bitset<10> a, b;
  a.set(1);
  vapor::Swap(a, b);
  EXPECT_TRUE(a.none());
  EXPECT_TRUE(b.test(1));
}
