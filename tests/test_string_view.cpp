#include <vapor/string_view.hpp>

#include <gtest/gtest.h>

#if defined(VAPOR_USE_STL)
#include <string>
#if (__cplusplus >= 201703L)
#include <string_view>
#endif
#endif

TEST(StringViewTest, Constructors) {
  // Empty state
  vapor::StringView empty_sv;
  EXPECT_EQ(empty_sv.size(), 0U);
  EXPECT_TRUE(empty_sv.empty());
  EXPECT_EQ(empty_sv.data(), nullptr);

  // C-string literal deduction
  vapor::StringView lit_sv("vapor");
  EXPECT_EQ(lit_sv.size(), 5U);
  EXPECT_STREQ(lit_sv.data(), "vapor");

  // Explicit size specification
  vapor::StringView explicit_sv("vapor_engine", 5);
  EXPECT_EQ(explicit_sv.size(), 5U);
  // Explicit bounding prevents seeing the trailing characters via string view
  // logic
  EXPECT_EQ(explicit_sv[4], 'r');
}

TEST(StringViewTest, STLInterop) {
#if defined(VAPOR_USE_STL)
  // std::string conversion / assignment
  std::string std_str = "nostl_fallback";
  vapor::StringView from_str(std_str);
  EXPECT_EQ(from_str.size(), std_str.size());
  EXPECT_EQ(from_str.data(), std_str.data());

  // Explicit round-trip conversion operator
  std::string round_trip = static_cast<std::string>(from_str);
  EXPECT_EQ(round_trip, std_str);

#if (__cplusplus >= 201703L)
  // Modern std::string_view interop validation
  std::string_view modern_view = "modern_cpp";
  vapor::StringView vapor_view(modern_view);
  EXPECT_EQ(vapor_view.size(), modern_view.size());
  EXPECT_EQ(vapor_view.data(), modern_view.data());
#endif
#else
  SUCCEED()
      << "Skipping STL interop assertions: compiled in pure nostl environment.";
#endif
}

TEST(StringViewTest, ElementAccess) {
  vapor::StringView sv("abcdef");

  EXPECT_EQ(sv[0], 'a');
  EXPECT_EQ(sv.at(5), 'f');
  EXPECT_EQ(sv.front(), 'a');
  EXPECT_EQ(sv.back(), 'f');
}

TEST(StringViewTest, Iteration) {
  vapor::StringView sv("12345");

  size_t count = 0;
  char expected_char = '1';
  for (vapor::StringView::const_iterator it = sv.begin(); it != sv.end();
       ++it) {
    EXPECT_EQ(*it, expected_char);
    expected_char++;
    count++;
  }
  EXPECT_EQ(count, 5U);
}

TEST(StringViewTest, Modifiers) {
  vapor::StringView sv("---payload---");

  sv.remove_prefix(3);
  EXPECT_EQ(sv.size(), 10U);
  EXPECT_EQ(sv.front(), 'p');

  sv.remove_suffix(3);
  EXPECT_EQ(sv.size(), 7U);
  EXPECT_EQ(sv.back(), 'd');

  vapor::StringView other("target");
  sv.swap(other);
  EXPECT_EQ(sv.size(), 6U);
  EXPECT_EQ(other.size(), 7U);
}

// --- 4. Relational and Equality Comparisons ---
TEST(StringViewTest, Comparisons) {
  vapor::StringView alpha("abc");
  vapor::StringView beta("def");
  vapor::StringView alpha_long("abcd");
  vapor::StringView alpha_identical("abc");

  // Core Boolean Assertions
  EXPECT_TRUE(alpha == alpha_identical);
  EXPECT_TRUE(alpha != beta);
  EXPECT_TRUE(alpha < beta);
  EXPECT_TRUE(beta > alpha);

  // Size Tie-breaker Check
  EXPECT_TRUE(alpha < alpha_long);
  EXPECT_TRUE(alpha_long > alpha);
  EXPECT_TRUE(alpha <= alpha_identical);
  EXPECT_TRUE(alpha >= alpha_identical);
}

TEST(StringViewTest, InspectionMatch) {
  vapor::StringView filename("libvapor.so");

  // StringView based checks
  EXPECT_TRUE(filename.starts_with(vapor::StringView("lib")));
  EXPECT_TRUE(filename.ends_with(vapor::StringView(".so")));
  EXPECT_FALSE(filename.starts_with(vapor::StringView("app")));

  // Primitive char checks
  EXPECT_TRUE(filename.starts_with('l'));
  EXPECT_TRUE(filename.ends_with('o'));
}
