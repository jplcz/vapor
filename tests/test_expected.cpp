#include <vapor/expected.hpp>

#include <gtest/gtest.h>

struct ComplexType
{
    int id;
    static int constructor_count;
    static int destructor_count;

    ComplexType() : id(0) { constructor_count++; }
    ComplexType(int i) : id(i) { constructor_count++; }
    ComplexType(const ComplexType& other) : id(other.id) { constructor_count++; }

    ComplexType(ComplexType&& other) noexcept : id(other.id)
    {
        other.id = -1;
        constructor_count++;
    }

    ComplexType& operator=(const ComplexType& other)
    {
        id = other.id;
        return *this;
    }

    ComplexType& operator=(ComplexType&& other) noexcept
    {
        id = other.id;
        other.id = -1;
        return *this;
    }

    ~ComplexType() { destructor_count++; }

    static void Reset()
    {
        constructor_count = 0;
        destructor_count = 0;
    }
};

int ComplexType::constructor_count = 0;
int ComplexType::destructor_count = 0;


TEST(ExpectedTest, ValueStateAccessors)
{
    vapor::Expected<int, const char*> exp(42);

    EXPECT_TRUE(exp.has_value());
    EXPECT_TRUE(static_cast<bool>(exp));
    EXPECT_EQ(exp.value(), 42);
    EXPECT_EQ(*exp, 42);
}

TEST(ExpectedTest, ErrorStateAccessors)
{
    auto unex = vapor::MakeUnexpected(500);
    vapor::Expected<std::string, int> exp(unex);

    EXPECT_FALSE(exp.has_value());
    EXPECT_FALSE(static_cast<bool>(exp));
    EXPECT_EQ(exp.error(), 500);
}

TEST(ExpectedTest, ValueOrBehavior)
{
    vapor::Expected<int, int> success(10);
    vapor::Expected<int, int> failure(vapor::MakeUnexpected(404));

    EXPECT_EQ(success.value_or(20), 10);
    EXPECT_EQ(failure.value_or(20), 20);
}

TEST(ExpectedTest, ConstructorDestructorLifetimes)
{
    ComplexType::Reset();
    {
        vapor::Expected<ComplexType, int> exp(ComplexType(123));
        EXPECT_TRUE(exp.has_value());
        EXPECT_EQ(exp.value().id, 123);
    }
    // Verifies placement new created it and the union properly destroyed it
    EXPECT_EQ(ComplexType::constructor_count, ComplexType::destructor_count);
}

TEST(ExpectedTest, CopyAndMoveSemantics)
{
    vapor::Expected<int, int> original(100);

    // Copy construction
    vapor::Expected<int, int> copy_constructed(original);
    EXPECT_EQ(copy_constructed.value(), 100);

    // Move construction
    vapor::Expected<int, int> move_constructed(static_cast<vapor::Expected<int, int>&&>(original));
    EXPECT_EQ(move_constructed.value(), 100);

    // Assignment operators
    vapor::Expected<int, int> copy_assigned(0);
    copy_assigned = copy_constructed;
    EXPECT_EQ(copy_assigned.value(), 100);

    vapor::Expected<int, int> move_assigned(0);
    move_assigned = static_cast<vapor::Expected<int, int>&&>(copy_assigned);
    EXPECT_EQ(move_assigned.value(), 100);
}

TEST(ExpectedTest, STLInteroperability)
{
#if defined(VAPOR_HAS_STD_EXPECTED)
    // Convert std::expected -> vapor::Expected
    std::expected<int, int> std_exp = 777;
    vapor::Expected<int, int> vapor_exp(std_exp);
    EXPECT_TRUE(vapor_exp.has_value());
    EXPECT_EQ(vapor_exp.value(), 777);

    // Convert vapor::Expected -> std::expected
    vapor::Expected<int, int> vapor_err(vapor::MakeUnexpected(404));
    std::expected<int, int> std_err = vapor_err;
    EXPECT_FALSE(std_err.has_value());
    EXPECT_EQ(std_err.error(), 404);
#else
    SUCCEED() << "Skipping STL interop tests: Baseline standard is below C++23 or VAPOR_USE_STL is disabled.";
#endif
}
