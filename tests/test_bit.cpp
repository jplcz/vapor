#include <vapor/bit.hpp>

#include <gtest/gtest.h>

using namespace vapor;

TEST(BitTest, PopCountAndZeroCounts) {
  EXPECT_EQ(PopCount(0u), 0);
  EXPECT_EQ(PopCount(0xF0F0u), 8);
  EXPECT_EQ(PopCount(static_cast<uint8_t>(0xFF)), 8);
  EXPECT_EQ(PopCount(~uint64_t(0)), 64);
  EXPECT_EQ(CountTrailingZeros(8u), 3);
  EXPECT_EQ(CountTrailingZeros(0u), 32);
  EXPECT_EQ(CountLeadingZeros(static_cast<uint8_t>(1)), 7);
  EXPECT_EQ(CountLeadingZeros(uint64_t(0)), 64);
  EXPECT_EQ(CountTrailingOnes(0b0111u), 3);
  EXPECT_EQ(CountLeadingOnes(static_cast<uint8_t>(0xF0)), 4);
}

TEST(BitTest, WidthAndPowersOfTwo) {
  EXPECT_EQ(BitWidth(0u), 0);
  EXPECT_EQ(BitWidth(255u), 8);
  EXPECT_TRUE(IsPowerOfTwo(64u));
  EXPECT_FALSE(IsPowerOfTwo(0u));
  EXPECT_FALSE(IsPowerOfTwo(65u));
  EXPECT_EQ(BitFloor(100u), 64u);
  EXPECT_EQ(BitFloor(0u), 0u);
  EXPECT_EQ(BitCeil(100u), 128u);
  EXPECT_EQ(BitCeil(64u), 64u);
  EXPECT_EQ(BitCeil(0u), 1u);
}

TEST(BitTest, BitCeilReturnsZeroOnOverflow) {
  EXPECT_EQ(BitCeil(static_cast<uint8_t>(200)), 0);
}

TEST(BitTest, RotateAndByteSwap) {
  EXPECT_EQ(RotateLeft(static_cast<uint8_t>(0x81), 1), 0x03);
  EXPECT_EQ(RotateRight(static_cast<uint8_t>(0x81), 1), 0xC0);
  EXPECT_EQ(RotateLeft(0x12345678u, 0), 0x12345678u);
  EXPECT_EQ(RotateLeft(0x12345678u, 32), 0x12345678u);
  EXPECT_EQ(RotateLeft(0x12345678u, -4), RotateRight(0x12345678u, 4));
  EXPECT_EQ(ByteSwap(0x11223344u), 0x44332211u);
  EXPECT_EQ(ByteSwap(static_cast<uint16_t>(0x1122)), 0x2211);
  EXPECT_EQ(ByteSwap(uint64_t(0x0102030405060708ULL)), 0x0807060504030201ULL);
}

TEST(BitTest, BitCastReinterpretsBytes) {
  EXPECT_EQ(BitCast<uint32_t>(1.0f), 0x3F800000u);
  EXPECT_EQ(BitCast<float>(0x40000000u), 2.0f);
}
