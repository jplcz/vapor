#include <vapor/string.hpp>

#include <gtest/gtest.h>

#if defined(VAPOR_USE_STL)
#include <string>
#include <unordered_set>
#endif

TEST(StringTest, DefaultConstructorEmptyState) {
  vapor::String str;

  EXPECT_TRUE(str.empty());
  EXPECT_EQ(str.size(), 0U);
  EXPECT_EQ(str.capacity(), 0U);
  EXPECT_STREQ(str.c_str(), "");
  EXPECT_STREQ(str.data(), "");
}

TEST(StringTest, FallibleReserveAndGrowth) {
  vapor::String str;

  // Test explicit memory registration
  auto res = str.reserve(16);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(str.capacity(), 16U);
  EXPECT_TRUE(str.empty());

  // Fill buffer up to capacity limit without reallocating
  for (char c = 'a'; c < 'a' + 16; ++c) {
    auto push_res = str.push_back(c);
    ASSERT_TRUE(push_res.has_value());
  }
  EXPECT_EQ(str.size(), 16U);
  EXPECT_EQ(str.capacity(), 16U);

  // This push should force a geometric capacity doubling
  auto force_grow = str.push_back('!');
  ASSERT_TRUE(force_grow.has_value());
  EXPECT_GT(str.capacity(), 16U);
  EXPECT_EQ(str.size(), 17U);
}

TEST(StringTest, AppendAndEraseBlockDataOperations) {
  vapor::String str;

  // Append standard string view slices
  auto app1 = str.append(vapor::StringView("Vapor"));
  ASSERT_TRUE(app1.has_value());
  auto app2 = str.append(vapor::StringView("Engine"));
  ASSERT_TRUE(app2.has_value());

  EXPECT_STREQ(str.c_str(), "VaporEngine");
  EXPECT_EQ(str.size(), 11U);

  // Test in-place memory shifting erasure via memmove
  auto erase_res = str.erase(5, 6); // Erase "Engine"
  ASSERT_TRUE(erase_res.has_value());
  EXPECT_STREQ(str.c_str(), "Vapor");
  EXPECT_EQ(str.size(), 5U);

  // Erase out of bounds check
  auto bad_erase = str.erase(10, 2);
  EXPECT_FALSE(bad_erase.has_value());
  EXPECT_EQ(bad_erase.error(), vapor::StringError::OutOfBounds);
}

TEST(StringTest, CheckedElementAccess) {
  vapor::String str;
  ASSERT_TRUE(str.append(vapor::StringView("XYZ")));

  auto char_good = str.checked_at(1);
  ASSERT_TRUE(char_good.has_value());
  EXPECT_EQ(static_cast<char>((*char_good)), 'Y');

  // Mutate character inside container safely using ReferenceWrapper
  (**char_good) = 'W';
  EXPECT_STREQ(str.c_str(), "XWZ");

  auto char_bad = str.checked_at(5);
  EXPECT_FALSE(char_bad.has_value());
  EXPECT_EQ(char_bad.error(), vapor::StringError::OutOfBounds);
}

TEST(StringTest, MoveSemanticsAndExplicitCloning) {
  vapor::String original;
  ASSERT_TRUE(original.append(vapor::StringView("Immutable")));

  // Move Construction Verification (Zero Allocation Transfer)
  vapor::String moved_to(std::move(original));
  EXPECT_STREQ(moved_to.c_str(), "Immutable");
  EXPECT_TRUE(original.empty()); // Source must be cleanly stripped

  // Deep copy verification via factory clone method
  auto clone_res = moved_to.Clone();
  ASSERT_TRUE(clone_res.has_value());

  vapor::String clone_str = std::move(*clone_res);
  EXPECT_STREQ(clone_str.c_str(), "Immutable");

  // Ensure memory arrays point to entirely unique spaces
  EXPECT_NE(clone_str.data(), moved_to.data());
}

// ============================================================================
// STL Integration Tests (Only parsed if VAPOR_USE_STL flag matches active
// compiler)
// ============================================================================
#if defined(VAPOR_USE_STL)

TEST(StringStlIntegrationTest, FallibleBasicStringFactoryAndCast) {
  std::string stl_string = "Legacy STL Data";

  auto ingest_res = vapor::String::FromStdString(stl_string);
  ASSERT_TRUE(ingest_res.has_value());
  EXPECT_STREQ(ingest_res->c_str(), "Legacy STL Data");

  auto round_trip = static_cast<std::basic_string<char>>(*ingest_res);
  EXPECT_EQ(round_trip, stl_string);

  auto assign_res = ingest_res->assign(std::basic_string<char>("New Context"));
  ASSERT_TRUE(assign_res.has_value());
  EXPECT_STREQ(ingest_res->c_str(), "New Context");
}

TEST(StringStlIntegrationTest, UnorderedSetHashingValidation) {
  std::unordered_set<vapor::String> custom_set;

  vapor::String key1;
  ASSERT_TRUE(key1.append(vapor::StringView("Alpha")));
  vapor::String key2;
  ASSERT_TRUE(key2.append(vapor::StringView("Beta")));

  custom_set.insert(std::move(key1));
  custom_set.insert(std::move(key2));

  // Create tracking lookup keys
  vapor::String lookup;
  ASSERT_TRUE(lookup.append(vapor::StringView("Alpha")));

  // Verifies std::hash<vapor::String> interface resolves correctly
  EXPECT_EQ(custom_set.count(lookup), 1U);
}
#endif // VAPOR_USE_STL

TEST(StringTest, RelationalOperatorsValidation) {
  vapor::String alpha;
  ASSERT_TRUE(alpha.append(vapor::StringView("abc")));

  vapor::String beta;
  ASSERT_TRUE(beta.append(vapor::StringView("xyz")));

  vapor::String alpha_duplicate;
  ASSERT_TRUE(alpha_duplicate.append(vapor::StringView("abc")));

  // String vs String
  EXPECT_TRUE(alpha == alpha_duplicate);
  EXPECT_TRUE(alpha != beta);
  EXPECT_TRUE(alpha < beta);
  EXPECT_TRUE(beta > alpha);
  EXPECT_TRUE(alpha <= alpha_duplicate);
  EXPECT_TRUE(alpha >= alpha_duplicate);

  // String vs StringView
  vapor::StringView view("abc");
  EXPECT_TRUE(alpha == view);
  EXPECT_TRUE(view == alpha);
  EXPECT_TRUE(beta != view);
  EXPECT_TRUE(alpha <= view);

  // String vs raw C-String
  EXPECT_TRUE(alpha == "abc");
  EXPECT_TRUE("abc" == alpha);
  EXPECT_TRUE(beta != "abc");
  EXPECT_TRUE(alpha < "xyz");
  EXPECT_TRUE("xyz" > alpha);
}

TEST(StringTest, TryDataHandling) {
  vapor::String empty_str;
  auto fail_res = empty_str.try_data();

  EXPECT_FALSE(fail_res.has_value());
  EXPECT_EQ(fail_res.error(), vapor::StringError::EmptyString);

  vapor::String populated_str;
  ASSERT_TRUE(populated_str.append(vapor::StringView("Vapor")).has_value());

  auto success_res = populated_str.try_data();
  ASSERT_TRUE(success_res.has_value());

  // Verify it points to the exact same memory location and contents
  EXPECT_EQ(*success_res, populated_str.data());
  EXPECT_STREQ(*success_res, "Vapor");

  // Verify it's a mutable pointer by modifying the first character directly
  (*success_res)[0] = 'v';
  EXPECT_STREQ(populated_str.c_str(), "vapor");
}
