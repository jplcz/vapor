#include <vapor/bit_view.hpp>

#include <gtest/gtest.h>

using namespace vapor;

TEST(BitViewTest, SetTestResetFlipUseLsbFirstOrder) {
  uint8_t buf[4] = {};
  BitView v(buf, 4);
  EXPECT_EQ(v.size(), 32U);
  v.set(0);
  v.set(9);
  EXPECT_EQ(buf[0], 0x01);
  EXPECT_EQ(buf[1], 0x02);
  EXPECT_TRUE(v.test(9));
  EXPECT_TRUE(v[0]);
  v.flip(9);
  EXPECT_FALSE(v.test(9));
  v.set(0, false);
  v.reset(1);
  EXPECT_EQ(buf[0], 0x00);
}

TEST(BitViewTest, CountFindAndFill) {
  uint8_t buf[3] = {};
  BitView v(buf, 3);
  EXPECT_TRUE(v.none());
  EXPECT_EQ(v.FindFirst(), BitView::npos);
  v.set(5);
  v.set(17);
  EXPECT_EQ(v.count(), 2U);
  EXPECT_TRUE(v.any());
  EXPECT_EQ(v.FindFirst(), 5U);
  EXPECT_EQ(v.FindNext(5), 17U);
  EXPECT_EQ(v.FindNext(17), BitView::npos);
  EXPECT_EQ(v.FindFirstZero(), 0U);
  v.Fill(true);
  EXPECT_EQ(v.count(), 24U);
  EXPECT_EQ(v.FindFirstZero(), BitView::npos);
  v.Fill(false);
  EXPECT_TRUE(v.none());
}

TEST(BitViewTest, ExplicitBitCountIsClamped) {
  uint8_t buf[2] = {0xFF, 0xFF};
  BitView v(Span<uint8_t>(buf, 2), 10);
  EXPECT_EQ(v.size(), 10U);
  EXPECT_EQ(v.count(), 10U);
  BitView clamped(Span<uint8_t>(buf, 2), 100);
  EXPECT_EQ(clamped.size(), 16U);
}

TEST(BitViewTest, CheckedTestReportsOutOfBounds) {
  uint8_t buf[1] = {0x80};
  BitView v(buf, 1);
  auto ok = v.checked_test(7);
  ASSERT_TRUE(ok.has_value());
  EXPECT_TRUE(ok.value());
  auto bad = v.checked_test(8);
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error(), CommonError::OutOfBounds);
}

TEST(BitViewTest, MutableConvertsToConstAndEmptyViewIsEmpty) {
  uint8_t buf[1] = {0x04};
  BitView v(buf, 1);
  ConstBitView c(v);
  EXPECT_TRUE(c.test(2));
  EXPECT_EQ(c.count(), 1U);
  BitView empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.count(), 0U);
}
