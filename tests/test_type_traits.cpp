#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

#include <gtest/gtest.h>

namespace {
struct Base {};
struct Derived : Base {};
struct NonTrivial {
  NonTrivial(const NonTrivial &) {}
};
struct Tracker {
  int copies = 0;
  int moves = 0;
  Tracker() = default;
  Tracker(const Tracker &o) : copies(o.copies + 1), moves(o.moves) {}
  Tracker(Tracker &&o) noexcept : copies(o.copies), moves(o.moves + 1) {}
};

Tracker Sink(Tracker t) { return t; }
template <typename T> Tracker Fwd(T &&t) { return Sink(vapor::Forward<T>(t)); }
void Fn(int) {}
} // namespace

using namespace vapor;

TEST(TypeTraitsTest, Basics) {
  static_assert(IsSame<int, int>::value, "");
  static_assert(!IsSame<int, long>::value, "");
  static_assert(IsSame<RemoveReferenceT<int &&>, int>::value, "");
  static_assert(IsSame<RemoveCVT<const volatile int>, int>::value, "");
  static_assert(IsSame<ConditionalT<true, int, char>, int>::value, "");
  static_assert(IsSame<EnableIfT<true, char>, char>::value, "");
  static_assert(IsSame<AddLvalueReferenceT<int>, int &>::value, "");
  static_assert(IsSame<AddRvalueReferenceT<void>, void>::value, "");
  static_assert(IsReference<int &>::value && !IsReference<int>::value, "");
  static_assert(IsPointer<int *const>::value, "");
  static_assert(IsConst<const int>::value, "");
  SUCCEED();
}

TEST(TypeTraitsTest, Decay) {
  static_assert(IsSame<DecayT<const int &>, int>::value, "");
  static_assert(IsSame<DecayT<int[3]>, int *>::value, "");
  static_assert(IsSame<DecayT<void(int)>, void (*)(int)>::value, "");
  (void)&Fn;
  SUCCEED();
}

TEST(TypeTraitsTest, Queries) {
  static_assert(IsTriviallyCopyable<int>::value, "");
  static_assert(!IsTriviallyCopyable<NonTrivial>::value, "");
  static_assert(IsBaseOf<Base, Derived>::value, "");
  static_assert(!IsBaseOf<Derived, Base>::value, "");
  static_assert(IsConvertible<Derived *, Base *>::value, "");
  static_assert(!IsConvertible<Base *, Derived *>::value, "");
  static_assert(IsConstructible<int, char>::value, "");
  static_assert(IsNothrowMoveConstructible<Tracker>::value, "");
  static_assert(!IsNothrowMoveConstructible<NonTrivial>::value, "");
  SUCCEED();
}

enum class Color : short { Red };
union U {
  int i;
};
struct Poly {
  virtual ~Poly() {}
};
struct Abstract {
  virtual void F() = 0;
};
struct Fin final {};

TEST(TypeTraitsTest, IntrinsicQueries) {
  static_assert(IsEnum<Color>::value && !IsEnum<int>::value, "");
  static_assert(IsUnion<U>::value && IsClass<Base>::value, "");
  static_assert(IsPolymorphic<Poly>::value && !IsPolymorphic<Base>::value, "");
  static_assert(IsAbstract<Abstract>::value, "");
  static_assert(IsFinal<Fin>::value && !IsFinal<Base>::value, "");
  static_assert(IsEmpty<Base>::value && !IsEmpty<Tracker>::value, "");
  static_assert(IsStandardLayout<Base>::value && !IsStandardLayout<Poly>::value,
                "");
  static_assert(IsTrivial<int>::value && IsPod<int>::value, "");
  static_assert(IsTriviallyDestructible<int>::value, "");
  static_assert(IsTriviallyDefaultConstructible<int>::value, "");
  static_assert(IsTriviallyAssignable<int &, int>::value, "");
  static_assert(!IsTriviallyAssignable<Tracker &, const Tracker &>::value, "");
  static_assert(IsNothrowConstructible<Tracker, Tracker &&>::value, "");
  static_assert(!IsNothrowConstructible<NonTrivial, const NonTrivial &>::value,
                "");
  static_assert(IsNothrowAssignable<int &, int>::value, "");
  static_assert(IsSame<UnderlyingTypeT<Color>, short>::value, "");
  SUCCEED();
}

TEST(UtilityTest, MoveForwardSwapExchange) {
  Tracker t;
  EXPECT_EQ(Fwd(t).copies, 1); // lvalue is copied once into the parameter
  Tracker moved(Move(t));
  EXPECT_EQ(moved.moves, 1);
  EXPECT_EQ(Fwd(Tracker()).copies, 0);

  int a = 1, b = 2;
  Swap(a, b);
  EXPECT_EQ(a, 2);
  EXPECT_EQ(b, 1);
  EXPECT_EQ(Exchange(a, 9), 2);
  EXPECT_EQ(a, 9);
  EXPECT_EQ(AddressOf(a), &a);
}
