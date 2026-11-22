#pragma once
#include <stddef.h>
#include <vapor/config.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// Iterator helpers: Distance/Advance/Next/Prev, ReverseIterator, IteratorRange,
// Reversed() and Enumerate().
//
// Why: algorithms and range-for loops work on pairs of iterators. These helpers
// cover the common needs (step an iterator, walk a container backwards, get
// the index while looping) without the STL.
//
// How:
//   for (auto &x : Reversed(vec)) ...            // last to first
//   for (auto e : Enumerate(vec)) use(e.index, e.value);
//   auto it = Next(vec.begin(), 2);
//
// Iterators need `operator++`, `operator*` and `operator!=`; random-access
// iterators (pointers, Vector::iterator) additionally get O(1) stepping.
// Reversed()/Enumerate() keep a reference to the container: do not pass a
// temporary that dies before the loop ends.

namespace vapor {

namespace detail {
template <typename It>
auto IsRandomAccess(int)
    -> decltype(static_cast<void>(Declval<It &>() += 1),
                static_cast<void>(Declval<It &>() - Declval<It &>()),
                TrueType());
template <typename It> FalseType IsRandomAccess(...);

template <typename It>
auto IsBidirectional(int)
    -> decltype(static_cast<void>(--Declval<It &>()), TrueType());
template <typename It> FalseType IsBidirectional(...);
} // namespace detail

template <typename It>
struct IsRandomAccessIterator : decltype(detail::IsRandomAccess<It>(0)) {};
template <typename It>
struct IsBidirectionalIterator : decltype(detail::IsBidirectional<It>(0)) {};

namespace detail {
template <typename It> ptrdiff_t DistanceImpl(It first, It last, TrueType) {
  return static_cast<ptrdiff_t>(last - first);
}
template <typename It> ptrdiff_t DistanceImpl(It first, It last, FalseType) {
  ptrdiff_t n = 0;
  for (; first != last; ++first)
    ++n;
  return n;
}
template <typename It> void AdvanceImpl(It &it, ptrdiff_t n, TrueType) {
  it += n;
}
template <typename It> void AdvanceImpl(It &it, ptrdiff_t n, FalseType) {
  for (; n > 0; --n)
    ++it;
  for (; n < 0; ++n)
    --it; // only valid for bidirectional iterators
}
} // namespace detail

template <typename It> VAPOR_NODISCARD ptrdiff_t Distance(It first, It last) {
  return detail::DistanceImpl(
      first, last, IntegralConstant<bool, IsRandomAccessIterator<It>::value>());
}
template <typename It> void Advance(It &it, ptrdiff_t n) {
  detail::AdvanceImpl(
      it, n, IntegralConstant<bool, IsRandomAccessIterator<It>::value>());
}
template <typename It> VAPOR_NODISCARD It Next(It it, ptrdiff_t n = 1) {
  Advance(it, n);
  return it;
}
template <typename It> VAPOR_NODISCARD It Prev(It it, ptrdiff_t n = 1) {
  Advance(it, -n);
  return it;
}

// Walks a bidirectional range backwards. Dereferencing yields the element
// before the wrapped iterator, exactly like std::reverse_iterator.
template <typename It> class ReverseIterator {
public:
  typedef It iterator_type;
  ReverseIterator() : it_() {}
  explicit ReverseIterator(It it) : it_(it) {}
  template <typename U>
  ReverseIterator(const ReverseIterator<U> &o) : it_(o.base()) {}

  It base() const { return it_; }
  auto operator*() const -> decltype(*Declval<It &>()) {
    It t = it_;
    --t;
    return *t;
  }
  ReverseIterator &operator++() {
    --it_;
    return *this;
  }
  ReverseIterator operator++(int) {
    ReverseIterator t(*this);
    --it_;
    return t;
  }
  ReverseIterator &operator--() {
    ++it_;
    return *this;
  }
  ReverseIterator operator--(int) {
    ReverseIterator t(*this);
    ++it_;
    return t;
  }
  ReverseIterator &operator+=(ptrdiff_t n) {
    it_ -= n;
    return *this;
  }
  ReverseIterator &operator-=(ptrdiff_t n) {
    it_ += n;
    return *this;
  }
  ReverseIterator operator+(ptrdiff_t n) const {
    return ReverseIterator(it_ - n);
  }
  ReverseIterator operator-(ptrdiff_t n) const {
    return ReverseIterator(it_ + n);
  }
  auto operator[](ptrdiff_t n) const -> decltype(*Declval<It &>()) {
    return *(*this + n);
  }

private:
  It it_;
};

template <typename A, typename B>
bool operator==(const ReverseIterator<A> &a, const ReverseIterator<B> &b) {
  return a.base() == b.base();
}
template <typename A, typename B>
bool operator!=(const ReverseIterator<A> &a, const ReverseIterator<B> &b) {
  return !(a.base() == b.base());
}
template <typename A, typename B>
bool operator<(const ReverseIterator<A> &a, const ReverseIterator<B> &b) {
  return b.base() < a.base();
}
template <typename A, typename B>
ptrdiff_t operator-(const ReverseIterator<A> &a, const ReverseIterator<B> &b) {
  return b.base() - a.base();
}

template <typename It>
VAPOR_NODISCARD ReverseIterator<It> MakeReverseIterator(It it) {
  return ReverseIterator<It>(it);
}

// A pair of iterators usable in a range-for loop.
template <typename It> class IteratorRange {
public:
  IteratorRange(It first, It last) : first_(first), last_(last) {}
  It begin() const { return first_; }
  It end() const { return last_; }
  bool empty() const { return first_ == last_; }
  size_t size() const { return static_cast<size_t>(Distance(first_, last_)); }

private:
  It first_;
  It last_;
};

template <typename It>
VAPOR_NODISCARD IteratorRange<It> MakeRange(It first, It last) {
  return IteratorRange<It>(first, last);
}

// Iterates `c` from the last element to the first.
template <typename C>
VAPOR_NODISCARD auto Reversed(C &c)
    -> IteratorRange<ReverseIterator<decltype(c.end())>> {
  typedef ReverseIterator<decltype(c.end())> R;
  return IteratorRange<R>(R(c.end()), R(c.begin()));
}

// One step of Enumerate(): the position and a reference to the element.
template <typename Ref> struct EnumerateItem {
  size_t index;
  Ref value;
};

template <typename It> class EnumerateIterator {
public:
  typedef decltype(*Declval<It &>()) Ref;
  EnumerateIterator(It it, size_t i) : it_(it), i_(i) {}
  EnumerateItem<Ref> operator*() const {
    EnumerateItem<Ref> r = {i_, *it_};
    return r;
  }
  EnumerateIterator &operator++() {
    ++it_;
    ++i_;
    return *this;
  }
  bool operator!=(const EnumerateIterator &o) const { return it_ != o.it_; }
  bool operator==(const EnumerateIterator &o) const { return it_ == o.it_; }

private:
  It it_;
  size_t i_;
};

// Loops over `c` giving `{index, value}` for every element.
template <typename C>
VAPOR_NODISCARD auto Enumerate(C &c)
    -> IteratorRange<EnumerateIterator<decltype(c.begin())>> {
  typedef EnumerateIterator<decltype(c.begin())> E;
  return IteratorRange<E>(E(c.begin(), 0), E(c.end(), 0));
}

} // namespace vapor
