#include <vapor/span.hpp>

#include <gtest/gtest.h>

TEST(SpanTest, Constructors) {
    // Empty state
    vapor::Span<int> empty_span;
    EXPECT_EQ(empty_span.size(), 0U);
    EXPECT_TRUE(empty_span.empty());
    EXPECT_EQ(empty_span.data(), nullptr);

    // Pointer + Count layout tracking
    int arr[] = {10, 20, 30, 40, 50};
    vapor::Span<int> ptr_count_span(arr, 3);
    EXPECT_EQ(ptr_count_span.size(), 3U);
    EXPECT_EQ(ptr_count_span[0], 10);
    EXPECT_EQ(ptr_count_span[2], 30);

    // First + Last pointer bounds tracking
    vapor::Span<int> range_span(arr, arr + 5);
    EXPECT_EQ(range_span.size(), 5U);
    EXPECT_EQ(range_span.back(), 50);

    // Fixed array C-style type deduction matching
    vapor::Span<int> deduced_span(arr);
    EXPECT_EQ(deduced_span.size(), 5U);
    EXPECT_EQ(deduced_span.size_bytes(), 5 * sizeof(int));
}

TEST(SpanTest, ConstPropagation) {
    int arr[] = {1, 2, 3};
    vapor::Span<const int> const_span(arr);

    EXPECT_EQ(const_span[0], 1);
    // Compiles perfectly as read-only view over mutable storage
    static_assert(std::is_const<std::remove_reference<decltype(const_span[0])>::type>::value, "Elements should be const");
}

TEST(SpanTest, STLInterop) {
#if defined(VAPOR_HAS_STD_SPAN)
    int arr[] = {100, 200, 300};
    std::span<int> std_s(arr);

    // Construct wrapper from standard layout target
    vapor::Span<int> vapor_s(std_s);
    EXPECT_EQ(vapor_s.size(), std_s.size());
    EXPECT_EQ(vapor_s.data(), std_s.data());

    // Explicit round-trip conversion testing
    std::span<int> round_trip = vapor_s;
    EXPECT_EQ(round_trip.size(), 3U);
    EXPECT_EQ(round_trip.data(), arr);
#else
    SUCCEED() << "Skipping std::span integration tests: Baseline compiler is below C++20 or VAPOR_USE_STL is disabled.";
#endif
}

TEST(SpanTest, ElementAccess) {
    int arr[] = {5, 10, 15};
    vapor::Span<int> s(arr);

    EXPECT_EQ(s[0], 5);
    EXPECT_EQ(s.at(1), 10);
    EXPECT_EQ(s.front(), 5);
    EXPECT_EQ(s.back(), 15);
}

TEST(SpanTest, Iteration) {
    int arr[] = {1, 2, 3};
    vapor::Span<int> s(arr);

    int sum = 0;
    for (vapor::Span<int>::iterator it = s.begin(); it != s.end(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 6);
}

TEST(SpanTest, Subviews) {
    int arr[] = {10, 20, 30, 40, 50};
    vapor::Span<int> s(arr);

    // subspan slicing mechanics
    vapor::Span<int> sub = s.subspan(1, 3);
    EXPECT_EQ(sub.size(), 3U);
    EXPECT_EQ(sub.front(), 20);
    EXPECT_EQ(sub.back(), 40);

    // first and last bound clamping blocks
    vapor::Span<int> head = s.first(2);
    EXPECT_EQ(head.size(), 2U);
    EXPECT_EQ(head.back(), 20);

    vapor::Span<int> tail = s.last(2);
    EXPECT_EQ(tail.size(), 2U);
    EXPECT_EQ(tail.front(), 40);
}

TEST(SpanTest, Modifiers) {
    int arr[] = {1, 2, 3, 4, 5};
    vapor::Span<int> s(arr);

    s.remove_prefix(1);
    EXPECT_EQ(s.size(), 4U);
    EXPECT_EQ(s.front(), 2);

    s.remove_suffix(1);
    EXPECT_EQ(s.size(), 3U);
    EXPECT_EQ(s.back(), 4);
}

TEST(SpanTest, Equality) {
    int arr1[] = {1, 2, 3};
    int arr2[] = {1, 2, 3};
    int arr3[] = {1, 2, 4};

    vapor::Span<int> s1(arr1);
    vapor::Span<int> s2(arr2);
    vapor::Span<int> s3(arr3);
    vapor::Span<int> s_short(arr1, 2);

    EXPECT_TRUE(s1 == s2);      // Mirror data payloads match
    EXPECT_FALSE(s1 == s3);     // Element data mismatch
    EXPECT_FALSE(s1 == s_short); // Length boundaries mismatch
    EXPECT_TRUE(s1 != s3);
}