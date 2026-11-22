#include <vapor/algorithm.hpp>
#include <vapor/array.hpp>
#include <vapor/vector.hpp>

#include <gtest/gtest.h>

using namespace vapor;

namespace {
Vector<int> Make(std::initializer_list<int> items) {
  Vector<int> v;
  for (int x : items)
    EXPECT_TRUE(v.push_back(x).has_value());
  return v;
}

Vector<int> Pseudo(int n) {
  Vector<int> v;
  for (int i = 0; i < n; ++i)
    EXPECT_TRUE(v.push_back((i * 7919) % 503).has_value());
  return v;
}

bool IsEven(int x) { return x % 2 == 0; }
int Twice(int x) { return x * 2; }
int AddInts(int a, int b) { return a + b; }
} // namespace

TEST(AlgorithmMoreTest, AccumulateForEachTransformIota) {
  Vector<int> v = Make({1, 2, 3, 4});
  EXPECT_EQ(Accumulate(v, 0), 10);
  EXPECT_EQ(Accumulate(v, 1, [](int a, int b) { return a * b; }), 24);

  int sum = 0;
  ForEach(v, [&sum](int x) { sum += x; });
  EXPECT_EQ(sum, 10);

  Vector<int> out;
  ASSERT_TRUE(out.resize(4).has_value());
  Transform(v.begin(), v.end(), out.begin(), Twice);
  EXPECT_EQ(out[3], 8);
  Transform(v.begin(), v.end(), out.begin(), out.begin(), AddInts);
  EXPECT_EQ(out[3], 12);

  Iota(out, 5);
  EXPECT_EQ(out[0], 5);
  EXPECT_EQ(out[3], 8);
}

TEST(AlgorithmMoreTest, CopyIfCopyNReplaceGenerate) {
  Vector<int> v = Make({1, 2, 3, 4, 5, 6});
  int evens[6] = {};
  int *end = CopyIf(v.begin(), v.end(), evens, IsEven);
  EXPECT_EQ(end - evens, 3);
  EXPECT_EQ(evens[2], 6);

  int first_two[2] = {};
  CopyN(v.begin(), 2, first_two);
  EXPECT_EQ(first_two[1], 2);

  Replace(v.begin(), v.end(), 3, 30);
  EXPECT_EQ(v[2], 30);

  int n = 0;
  Generate(v.begin(), v.end(), [&n] { return n++; });
  EXPECT_EQ(v[5], 5);
}

TEST(AlgorithmMoreTest, AdjacentFindAndSearch) {
  Vector<int> v = Make({1, 2, 2, 3});
  EXPECT_EQ(AdjacentFind(v.begin(), v.end()) - v.begin(), 1);
  Vector<int> none = Make({1, 2, 3});
  EXPECT_TRUE(AdjacentFind(none.begin(), none.end()) == none.end());

  Vector<int> hay = Make({1, 2, 3, 4, 3, 4, 5});
  Vector<int> needle = Make({3, 4, 5});
  EXPECT_EQ(Search(hay.begin(), hay.end(), needle.begin(), needle.end()) -
                hay.begin(),
            4);
  Vector<int> missing = Make({9});
  EXPECT_TRUE(Search(hay.begin(), hay.end(), missing.begin(),
                     missing.end()) == hay.end());
  EXPECT_TRUE(Search(hay.begin(), hay.end(), missing.begin(), missing.begin()) ==
              hay.begin());
}

TEST(AlgorithmMoreTest, PartitionFamily) {
  Vector<int> v = Pseudo(100);
  EXPECT_FALSE(IsPartitioned(v, IsEven));
  int *mid = Partition(v.begin(), v.end(), IsEven);
  EXPECT_TRUE(IsPartitioned(v, IsEven));
  EXPECT_EQ(mid - v.begin(), static_cast<ptrdiff_t>(CountIf(v, IsEven)));
  EXPECT_TRUE(PartitionPoint(v.begin(), v.end(), IsEven) == mid);
}

TEST(AlgorithmMoreTest, StablePartitionKeepsOrder) {
  Vector<int> v = Make({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  int *mid = StablePartition(v.begin(), v.end(), IsEven);
  EXPECT_EQ(mid - v.begin(), 5);
  int expected[10] = {2, 4, 6, 8, 10, 1, 3, 5, 7, 9};
  for (size_t i = 0; i < 10; ++i)
    EXPECT_EQ(v[i], expected[i]);

  Vector<int> big = Pseudo(300);
  Vector<int> evens, odds;
  for (int x : big)
    ASSERT_TRUE((IsEven(x) ? evens : odds).push_back(x).has_value());
  StablePartition(big.begin(), big.end(), IsEven);
  size_t i = 0;
  for (int x : evens)
    EXPECT_EQ(big[i++], x);
  for (int x : odds)
    EXPECT_EQ(big[i++], x);
}

TEST(AlgorithmMoreTest, PartitionCopySplitsOutput) {
  Vector<int> v = Make({1, 2, 3, 4, 5});
  int ev[5] = {}, od[5] = {};
  Pair<int *, int *> ends = PartitionCopy(v.begin(), v.end(), ev, od, IsEven);
  EXPECT_EQ(ends.first - ev, 2);
  EXPECT_EQ(ends.second - od, 3);
}

TEST(AlgorithmMoreTest, PartialSortAndNthElement) {
  Vector<int> v = Pseudo(200);
  Vector<int> sorted = Pseudo(200);
  Sort(sorted);

  PartialSort(v.begin(), v.begin() + 10, v.end());
  for (size_t i = 0; i < 10; ++i)
    EXPECT_EQ(v[i], sorted[i]);

  for (size_t nth : {size_t(0), size_t(1), size_t(99), size_t(199)}) {
    Vector<int> w = Pseudo(200);
    NthElement(w.begin(), w.begin() + static_cast<ptrdiff_t>(nth), w.end());
    EXPECT_EQ(w[nth], sorted[nth]);
    for (size_t i = 0; i < nth; ++i)
      EXPECT_LE(w[i], w[nth]);
    for (size_t i = nth; i < w.size(); ++i)
      EXPECT_GE(w[i], w[nth]);
  }

  Vector<int> same = Make({5, 5, 5, 5, 5, 5, 5, 5});
  NthElement(same.begin(), same.begin() + 3, same.end());
  EXPECT_EQ(same[3], 5);

  Vector<int> desc = Pseudo(50);
  NthElement(desc.begin(), desc.begin() + 4, desc.end(), Greater<int>());
  Vector<int> sd = Pseudo(50);
  Sort(sd, Greater<int>());
  EXPECT_EQ(desc[4], sd[4]);
}

TEST(AlgorithmMoreTest, MergeAndInplaceMerge) {
  Vector<int> a = Make({1, 3, 5, 7});
  Vector<int> b = Make({2, 3, 6});
  int out[7] = {};
  int *end = Merge(a.begin(), a.end(), b.begin(), b.end(), out);
  EXPECT_EQ(end - out, 7);
  EXPECT_TRUE(IsSorted(out, out + 7));

  Vector<int> v = Make({1, 4, 6, 2, 3, 5, 7});
  InplaceMerge(v.begin(), v.begin() + 3, v.end());
  EXPECT_TRUE(IsSorted(v));
  EXPECT_EQ(v.size(), 7u);
}

TEST(AlgorithmMoreTest, SetOperations) {
  Vector<int> a = Make({1, 2, 2, 3, 5});
  Vector<int> b = Make({2, 3, 3, 4});
  int out[10] = {};

  int *e = SetUnion(a.begin(), a.end(), b.begin(), b.end(), out);
  int uni[] = {1, 2, 2, 3, 3, 4, 5};
  ASSERT_EQ(e - out, 7);
  for (int i = 0; i < 7; ++i)
    EXPECT_EQ(out[i], uni[i]);

  e = SetIntersection(a.begin(), a.end(), b.begin(), b.end(), out);
  ASSERT_EQ(e - out, 2);
  EXPECT_EQ(out[0], 2);
  EXPECT_EQ(out[1], 3);

  e = SetDifference(a.begin(), a.end(), b.begin(), b.end(), out);
  int diff[] = {1, 2, 5};
  ASSERT_EQ(e - out, 3);
  for (int i = 0; i < 3; ++i)
    EXPECT_EQ(out[i], diff[i]);

  e = SetSymmetricDifference(a.begin(), a.end(), b.begin(), b.end(), out);
  int sym[] = {1, 2, 3, 4, 5};
  ASSERT_EQ(e - out, 5);
  for (int i = 0; i < 5; ++i)
    EXPECT_EQ(out[i], sym[i]);

  EXPECT_TRUE(Includes(a.begin(), a.end(), a.begin() + 1, a.begin() + 3));
  EXPECT_FALSE(Includes(a.begin(), a.end(), b.begin(), b.end()));
}

TEST(AlgorithmMoreTest, Permutations) {
  Array<int, 3> a = {{1, 2, 3}};
  int count = 1;
  while (NextPermutation(a))
    ++count;
  EXPECT_EQ(count, 6);
  EXPECT_EQ(a[0], 1);
  EXPECT_EQ(a[2], 3);

  Array<int, 3> d = {{3, 2, 1}};
  EXPECT_TRUE(PrevPermutation(d));
  EXPECT_EQ(d[0], 3);
  EXPECT_EQ(d[1], 1);
  EXPECT_EQ(d[2], 2);

  Array<int, 3> s = {{1, 2, 3}};
  EXPECT_FALSE(PrevPermutation(s));
  EXPECT_EQ(s[0], 3);

  int one[1] = {7};
  EXPECT_FALSE(NextPermutation(one, one + 1));
}
