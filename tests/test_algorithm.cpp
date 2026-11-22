#include <vapor/algorithm.hpp>
#include <vapor/array.hpp>
#include <vapor/inline_vector.hpp>
#include <vapor/vector.hpp>

#include "test_allocator.hpp"
#include <gtest/gtest.h>

namespace va = vapor;
using vapor_test::Tracked;

namespace {
unsigned g_seed = 12345;
unsigned Rand() {
  g_seed = g_seed * 1664525u + 1013904223u;
  return g_seed >> 8;
}

struct Item {
  int key;
  int order;
};
bool ByKey(const Item &a, const Item &b) { return a.key < b.key; }
bool Greater(int a, int b) { return a > b; }
bool IsEven(int x) { return x % 2 == 0; }
} // namespace

TEST(AlgorithmTest, MinMaxClamp) {
  EXPECT_EQ(va::Min(3, 5), 3);
  EXPECT_EQ(va::Max(3, 5), 5);
  EXPECT_EQ(va::Min(3, 5, Greater), 5);
  EXPECT_EQ(va::Clamp(10, 0, 5), 5);
  EXPECT_EQ(va::Clamp(-1, 0, 5), 0);
  EXPECT_EQ(va::Clamp(3, 0, 5), 3);
  int a[] = {4, 1, 9, 1};
  EXPECT_EQ(va::MinElement(a, a + 4), a + 1);
  EXPECT_EQ(va::MaxElement(a, a + 4), a + 2);
  EXPECT_EQ(va::MinElement(a, a), a);
}

TEST(AlgorithmTest, SearchAndPredicates) {
  int a[] = {1, 2, 3, 4, 2};
  EXPECT_EQ(va::Find(a, a + 5, 3), a + 2);
  EXPECT_EQ(va::Find(a, a + 5, 9), a + 5);
  EXPECT_EQ(va::FindIf(a, a + 5, IsEven), a + 1);
  EXPECT_EQ(va::FindIfNot(a, a + 5, IsEven), a);
  EXPECT_EQ(va::Count(a, a + 5, 2), 2U);
  EXPECT_EQ(va::CountIf(a, a + 5, IsEven), 3U);
  EXPECT_TRUE(va::AnyOf(a, a + 5, IsEven));
  EXPECT_FALSE(va::AllOf(a, a + 5, IsEven));
  EXPECT_FALSE(va::NoneOf(a, a + 5, IsEven));
  EXPECT_TRUE(va::AllOf(a, a, IsEven)); // empty range
}

TEST(AlgorithmTest, BinarySearchFamily) {
  int a[] = {1, 2, 2, 2, 5, 7};
  EXPECT_EQ(va::LowerBound(a, a + 6, 2) - a, 1);
  EXPECT_EQ(va::UpperBound(a, a + 6, 2) - a, 4);
  EXPECT_EQ(va::LowerBound(a, a + 6, 0) - a, 0);
  EXPECT_EQ(va::LowerBound(a, a + 6, 9) - a, 6);
  EXPECT_TRUE(va::BinarySearch(a, a + 6, 5));
  EXPECT_FALSE(va::BinarySearch(a, a + 6, 6));
  EXPECT_FALSE(va::BinarySearch(a, a, 1));

  int d[] = {7, 5, 2, 1};
  EXPECT_TRUE(va::BinarySearch(d, d + 4, 5, Greater));
  EXPECT_EQ(va::LowerBound(d, d + 4, 5, Greater) - d, 1);
}

TEST(AlgorithmTest, CompareFunctions) {
  int a[] = {1, 2, 3}, b[] = {1, 2, 4}, c[] = {1, 2, 3, 4};
  EXPECT_TRUE(va::Equal(a, a + 3, a));
  EXPECT_FALSE(va::Equal(a, a + 3, b));
  EXPECT_FALSE(va::Equal(a, a + 3, c, c + 4));
  EXPECT_TRUE(va::Equal(a, a + 3, c, c + 3));
  EXPECT_EQ(va::Mismatch(a, a + 3, b).first, a + 2);
  EXPECT_TRUE(va::LexicographicalCompare(a, a + 3, b, b + 3));
  EXPECT_TRUE(va::LexicographicalCompare(a, a + 3, c, c + 4));
  EXPECT_FALSE(va::LexicographicalCompare(c, c + 4, a, a + 3));
  EXPECT_TRUE(va::IsSorted(a, a + 3));
  int u[] = {2, 1};
  EXPECT_FALSE(va::IsSorted(u, u + 2));
  EXPECT_TRUE(va::IsSorted(u, u + 2, Greater));
}

TEST(AlgorithmTest, CopyFillReverseRotate) {
  int src[] = {1, 2, 3, 4, 5};
  int dst[5] = {};
  EXPECT_EQ(va::Copy(src, src + 5, dst), dst + 5);
  EXPECT_TRUE(va::Equal(src, src + 5, dst));

  // Overlapping copies in both directions (memmove fast path).
  int shift[] = {1, 2, 3, 4, 5, 0};
  va::CopyBackward(shift, shift + 5, shift + 6);
  EXPECT_EQ(shift[5], 5);
  EXPECT_EQ(shift[1], 1);
  int back[] = {0, 1, 2, 3, 4};
  va::Copy(back + 1, back + 5, back);
  EXPECT_EQ(back[0], 1);
  EXPECT_EQ(back[3], 4);

  char buf[5];
  va::Fill(buf, buf + 5, 'z');
  EXPECT_EQ(buf[4], 'z');
  int ints[3];
  va::Fill(ints, ints + 3, 7);
  EXPECT_EQ(ints[2], 7);

  va::Reverse(src, src + 5);
  EXPECT_EQ(src[0], 5);
  EXPECT_EQ(src[4], 1);
  va::Reverse(src, src + 4);
  EXPECT_EQ(src[0], 2);

  int r[] = {1, 2, 3, 4, 5};
  EXPECT_EQ(va::Rotate(r, r + 2, r + 5), r + 3);
  EXPECT_EQ(r[0], 3);
  EXPECT_EQ(r[3], 1);
  EXPECT_EQ(r[4], 2);

  int x[] = {1, 2}, y[] = {3, 4};
  va::SwapRanges(x, x + 2, y);
  EXPECT_EQ(x[0], 3);
  EXPECT_EQ(y[1], 2);
}

TEST(AlgorithmTest, RemoveAndUnique) {
  int a[] = {1, 2, 3, 2, 4, 2};
  int *end = va::Remove(a, a + 6, 2);
  ASSERT_EQ(end - a, 3);
  EXPECT_EQ(a[0], 1);
  EXPECT_EQ(a[1], 3);
  EXPECT_EQ(a[2], 4);

  int b[] = {1, 2, 3, 4, 5, 6};
  int *e2 = va::RemoveIf(b, b + 6, IsEven);
  ASSERT_EQ(e2 - b, 3);
  EXPECT_EQ(b[2], 5);

  int c[] = {1, 1, 2, 2, 2, 3, 1};
  int *e3 = va::Unique(c, c + 7);
  ASSERT_EQ(e3 - c, 4);
  EXPECT_EQ(c[0], 1);
  EXPECT_EQ(c[1], 2);
  EXPECT_EQ(c[2], 3);
  EXPECT_EQ(c[3], 1);
  EXPECT_EQ(va::Unique(c, c), c);
}

TEST(AlgorithmTest, SortMatchesReferenceOnManyShapes) {
  for (size_t n : {0u, 1u, 2u, 3u, 16u, 17u, 100u, 1000u, 5000u}) {
    for (int shape = 0; shape < 5; ++shape) {
      va::Vector<int> v;
      for (size_t i = 0; i < n; ++i) {
        int val = 0;
        switch (shape) {
        case 0: val = static_cast<int>(Rand() % 1000); break; // random
        case 1: val = static_cast<int>(i); break;              // sorted
        case 2: val = static_cast<int>(n - i); break;          // reversed
        case 3: val = static_cast<int>(Rand() % 3); break;     // few distinct
        case 4: val = 7; break;                                // all equal
        }
        ASSERT_TRUE(v.push_back(val));
      }
      va::Vector<int> expected = v.Clone().value();
      // Reference: insertion sort.
      for (size_t i = 1; i < expected.size(); ++i)
        for (size_t j = i; j > 0 && expected[j] < expected[j - 1]; --j)
          va::Swap(expected[j], expected[j - 1]);
      va::Sort(v);
      ASSERT_TRUE(v == expected) << "n=" << n << " shape=" << shape;
    }
  }
}

TEST(AlgorithmTest, SortWithComparatorAndAdversarialInput) {
  // "Organ pipe" input is a classic quicksort stress case.
  va::Vector<int> v;
  for (int i = 0; i < 2000; ++i)
    ASSERT_TRUE(v.push_back(i < 1000 ? i : 2000 - i));
  va::Sort(v, Greater);
  EXPECT_TRUE(va::IsSorted(v, Greater));
  EXPECT_EQ(v[0], 1000);
}

TEST(AlgorithmTest, SortWorksWithNonTrivialTypes) {
  Tracked::alive = 0;
  {
    va::Vector<Tracked> v;
    for (int i = 0; i < 300; ++i)
      ASSERT_TRUE(v.emplace_back(static_cast<int>(Rand() % 50)));
    va::Sort(v, [](const Tracked &a, const Tracked &b) {
      return a.value < b.value;
    });
    for (size_t i = 1; i < v.size(); ++i)
      EXPECT_LE(v[i - 1].value, v[i].value);
    EXPECT_EQ(Tracked::alive, 300);
  }
  EXPECT_EQ(Tracked::alive, 0);
}

TEST(AlgorithmTest, StableSortKeepsEqualKeysInOrder) {
  for (size_t n : {5u, 8u, 9u, 64u, 500u, 3000u}) {
    va::Vector<Item> v;
    for (size_t i = 0; i < n; ++i)
      ASSERT_TRUE(v.push_back(Item{static_cast<int>(Rand() % 10),
                                   static_cast<int>(i)}));
    va::StableSort(v, ByKey);
    for (size_t i = 1; i < v.size(); ++i) {
      ASSERT_LE(v[i - 1].key, v[i].key);
      if (v[i - 1].key == v[i].key)
        ASSERT_LT(v[i - 1].order, v[i].order) << "n=" << n;
    }
  }
}

TEST(AlgorithmTest, ContainerOverloads) {
  va::Array<int, 5> a = {{5, 3, 1, 4, 2}};
  va::Sort(a);
  EXPECT_TRUE(va::IsSorted(a));
  EXPECT_EQ(*va::Find(a, 4), 4);
  EXPECT_TRUE(va::BinarySearch(a, 3));
  EXPECT_EQ(va::LowerBound(a, 3) - a.begin(), 2);
  EXPECT_EQ(va::Count(a, 1), 1U);
  EXPECT_TRUE(va::AnyOf(a, IsEven));
  va::Reverse(a);
  EXPECT_EQ(a[0], 5);
  va::Fill(a, 0);
  EXPECT_EQ(a[4], 0);

  va::InlineVector<int, 4> iv;
  for (int i : {3, 1, 2})
    ASSERT_TRUE(iv.push_back(i));
  va::StableSort(iv);
  EXPECT_EQ(iv[0], 1);
  EXPECT_EQ(*va::MaxElement(iv), 3);

  va::Array<int, 2> p = {{1, 2}}, q = {{1, 2}};
  EXPECT_TRUE(va::Equal(p, q));
  const va::Array<int, 2> &cp = p;
  EXPECT_EQ(va::Find(cp, 2), cp.begin() + 1);
}
