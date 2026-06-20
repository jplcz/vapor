#include <gtest/gtest.h>
#include <vapor/string.hpp>
#include <vapor/vector.hpp>

struct TrackedElement {
  int id;
  static int active_instances;

  explicit TrackedElement(int val) : id(val) { active_instances++; }
  ~TrackedElement() { active_instances--; }

  // Copy construction
  TrackedElement(const TrackedElement &other) : id(other.id) {
    active_instances++;
  }

  // Move construction using raw rvalue cast (avoiding std::move)
  TrackedElement(TrackedElement &&other) noexcept : id(other.id) {
    other.id = -1;
  }

  // Move assignment using raw rvalue cast
  TrackedElement &operator=(TrackedElement &&other) noexcept {
    if (this != &other) {
      id = other.id;
      other.id = -1;
    }
    return *this;
  }
};
int TrackedElement::active_instances = 0;

TEST(VectorTest, DefaultConstructorEmptyState) {
  vapor::Vector<int> vec;

  EXPECT_TRUE(vec.empty());
  EXPECT_EQ(vec.size(), 0U);
  EXPECT_EQ(vec.capacity(), 0U);
  EXPECT_EQ(vec.data(), nullptr);
}

TEST(VectorTest, PushAndEmplaceGrowth) {
  TrackedElement::active_instances = 0;
  {
    vapor::Vector<TrackedElement> vec;

    // Test emplace_back perfect forwarding
    auto res1 = vec.emplace_back(10);
    ASSERT_TRUE(res1.has_value());

    // Test push_back with an explicit rvalue cast instead of std::move
    TrackedElement element(20);
    auto res2 = vec.push_back(static_cast<TrackedElement &&>(element));
    ASSERT_TRUE(res2.has_value());

    EXPECT_EQ(vec.size(), 2U);
    EXPECT_EQ(TrackedElement::active_instances, 2);
    EXPECT_EQ(vec.checked_at(0)->get().id, 10);
    EXPECT_EQ(vec.checked_at(1)->get().id, 20);

    // Force an explicit geometric storage capacity expansion
    auto res_grow = vec.reserve(8);
    ASSERT_TRUE(res_grow.has_value());
    EXPECT_GE(vec.capacity(), 8U);
    EXPECT_EQ(TrackedElement::active_instances,
              2); // Internal items must be safely relocated
  }
  // Leaving scope must automatically clear the vector and destroy all items
  // cleanly
  EXPECT_EQ(TrackedElement::active_instances, 0);
}

TEST(VectorTest, PopAndEraseMemoryShifting) {
  vapor::Vector<int> vec;
  ASSERT_TRUE(vec.push_back(100).has_value());
  ASSERT_TRUE(vec.push_back(200).has_value());
  ASSERT_TRUE(vec.push_back(300).has_value());

  // Test pop_back dropping the trailing element
  vec.pop_back();
  EXPECT_EQ(vec.size(), 2U);
  EXPECT_EQ((*vec.checked_at(0)).get(), 100);
  EXPECT_EQ((*vec.checked_at(1)).get(), 200);

  // Add another item back to test inner index erasure
  ASSERT_TRUE(vec.push_back(400).has_value()); // Layout: [100, 200, 400]

  // Erase the element at index 1 (200), shifting 400 down
  auto erase_res = vec.erase(1);
  ASSERT_TRUE(erase_res.has_value());
  EXPECT_EQ(vec.size(), 2U);
  EXPECT_EQ((*vec.checked_at(0)).get(), 100);
  EXPECT_EQ((*vec.checked_at(1)).get(), 400);

  // Ensure out of bounds erasure fails cleanly without panicking
  auto bad_erase = vec.erase(5);
  EXPECT_FALSE(bad_erase.has_value());
  EXPECT_EQ(bad_erase.error(), vapor::VectorError::OutOfBounds);
}

TEST(VectorTest, TryDataHandling) {
  vapor::Vector<int> empty_vec;
  auto fail_res1 = empty_vec.try_data();
  EXPECT_FALSE(fail_res1.has_value());
  EXPECT_EQ(fail_res1.error(), vapor::VectorError::EmptyVector);

  vapor::Vector<int> cleared_vec;
  ASSERT_TRUE(cleared_vec.push_back(42).has_value());
  cleared_vec.clear();

  auto fail_res2 = cleared_vec.try_data();
  EXPECT_FALSE(fail_res2.has_value());
  EXPECT_EQ(fail_res2.error(), vapor::VectorError::EmptyVector);

  vapor::Vector<int> active_vec;
  ASSERT_TRUE(active_vec.push_back(99).has_value());

  auto success_res = active_vec.try_data();
  ASSERT_TRUE(success_res.has_value());
  EXPECT_EQ(*success_res, active_vec.data());
  EXPECT_EQ((*success_res)[0], 99);
}

TEST(VectorTest, HybridCloneValidation) {
  vapor::Vector<int> pod_vec;
  ASSERT_TRUE(pod_vec.push_back(7).has_value());
  ASSERT_TRUE(pod_vec.push_back(8).has_value());

  auto pod_clone = pod_vec.Clone();
  ASSERT_TRUE(pod_clone.has_value());
  EXPECT_EQ(pod_clone->size(), 2U);
  EXPECT_EQ((*pod_clone->checked_at(1)).get(), 8);

  vapor::Vector<vapor::String> custom_strings;
  vapor::String str;
  ASSERT_TRUE(str.append("Framework").has_value());
  ASSERT_TRUE(
      custom_strings.push_back(static_cast<vapor::String &&>(str)).has_value());

  auto string_clone = custom_strings.Clone();
  ASSERT_TRUE(string_clone.has_value());
  EXPECT_EQ(string_clone->size(), 1U);
  EXPECT_STREQ((*string_clone->checked_at(0)).get().c_str(), "Framework");
}

TEST(VectorTest, RvalueCloneOptimization) {
  vapor::Vector<int> temporary_vec;
  ASSERT_TRUE(temporary_vec.push_back(1337).has_value());

  const int *original_address = temporary_vec.data();
  size_t original_size = temporary_vec.size();

  // Trigger const && overload via raw rvalue cast to avoid standard library
  // plumbing
  auto clone_res =
      static_cast<const vapor::Vector<int> &&>(temporary_vec).Clone();
  ASSERT_TRUE(clone_res.has_value());

  vapor::Vector<int> final_vec = static_cast<vapor::Vector<int> &&>(*clone_res);

  // Verify data transferred without spinning up new memory allocations
  EXPECT_EQ(final_vec.size(), original_size);
  EXPECT_EQ(final_vec.data(), original_address);

  // Source temporary container must be left completely empty and gut-stripped
  EXPECT_TRUE(temporary_vec.empty());
  EXPECT_EQ(temporary_vec.capacity(), 0U);
}