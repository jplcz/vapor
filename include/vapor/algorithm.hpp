#pragma once
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/config.hpp>
#include <vapor/pair.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// STL-free algorithms, named in PascalCase like the rest of the library.
//
// Why: sorting, searching and copying ranges are needed everywhere, and
// <algorithm> is not available in freestanding code. None of these allocate or
// throw.
//
// How: every function takes an iterator pair `(first, last)` like the STL, and
// most also accept a whole container or Span: `Sort(vec)`, `Find(span, 3)`,
// `BinarySearch(array, key)`. Comparators default to `operator<`.
//
// Iterator requirements: the searching/sorting family (LowerBound, UpperBound,
// BinarySearch, Sort, StableSort, MinElement, ...) needs random-access
// iterators (pointers, Vector/Span/Array iterators). The rest only need forward
// iterators. A "predicate" is anything callable returning something
// convertible to bool.
//
// Sort is introsort (quicksort that switches to heapsort when it recurses too
// deep and to insertion sort on small pieces), so the worst case is
// O(n log n) and it never allocates. StableSort keeps equal elements in their
// original order and also needs no memory, at the price of O(n log^2 n) time.

namespace vapor {

namespace detail {
typedef Less<void> LessOp;

template <typename It>
using IterValueT = RemoveCVT<RemoveReferenceT<decltype(*Declval<It &>())>>;

template <typename C> using RangeBeginT = decltype(Declval<C &>().begin());
} // namespace detail

// ---------------------------------------------------------------- min / max

// Clamp(v, lo, hi): v limited to [lo, hi]; lo must not be greater than hi.
template <typename T>
VAPOR_NODISCARD constexpr const T &Clamp(const T &v, const T &lo, const T &hi) {
  return v < lo ? lo : (hi < v ? hi : v);
}

template <typename It, typename Cmp>
VAPOR_NODISCARD It MinElement(It first, It last, Cmp cmp) {
  It best = first;
  if (first == last)
    return last;
  for (++first; first != last; ++first)
    if (cmp(*first, *best))
      best = first;
  return best;
}
template <typename It> VAPOR_NODISCARD It MinElement(It first, It last) {
  return MinElement(first, last, detail::LessOp());
}
template <typename It, typename Cmp>
VAPOR_NODISCARD It MaxElement(It first, It last, Cmp cmp) {
  It best = first;
  if (first == last)
    return last;
  for (++first; first != last; ++first)
    if (cmp(*best, *first))
      best = first;
  return best;
}
template <typename It> VAPOR_NODISCARD It MaxElement(It first, It last) {
  return MaxElement(first, last, detail::LessOp());
}

// ------------------------------------------------------------------- search

template <typename It, typename Pred>
VAPOR_NODISCARD It FindIf(It first, It last, Pred pred) {
  for (; first != last; ++first)
    if (pred(*first))
      return first;
  return last;
}
template <typename It, typename Pred>
VAPOR_NODISCARD It FindIfNot(It first, It last, Pred pred) {
  for (; first != last; ++first)
    if (!pred(*first))
      return first;
  return last;
}
template <typename It, typename T>
VAPOR_NODISCARD It Find(It first, It last, const T &value) {
  for (; first != last; ++first)
    if (*first == value)
      return first;
  return last;
}
template <typename It, typename T>
VAPOR_NODISCARD size_t Count(It first, It last, const T &value) {
  size_t n = 0;
  for (; first != last; ++first)
    if (*first == value)
      ++n;
  return n;
}
template <typename It, typename Pred>
VAPOR_NODISCARD size_t CountIf(It first, It last, Pred pred) {
  size_t n = 0;
  for (; first != last; ++first)
    if (pred(*first))
      ++n;
  return n;
}
template <typename It, typename Pred>
VAPOR_NODISCARD bool AnyOf(It first, It last, Pred pred) {
  return FindIf(first, last, pred) != last;
}
template <typename It, typename Pred>
VAPOR_NODISCARD bool AllOf(It first, It last, Pred pred) {
  return FindIfNot(first, last, pred) == last;
}
template <typename It, typename Pred>
VAPOR_NODISCARD bool NoneOf(It first, It last, Pred pred) {
  return FindIf(first, last, pred) == last;
}

// First element that is not ordered before `value` (needs a sorted range).
template <typename It, typename T, typename Cmp>
VAPOR_NODISCARD It LowerBound(It first, It last, const T &value, Cmp cmp) {
  size_t len = static_cast<size_t>(last - first);
  while (len > 0) {
    size_t half = len / 2;
    It mid = first + half;
    if (cmp(*mid, value)) {
      first = mid + 1;
      len -= half + 1;
    } else {
      len = half;
    }
  }
  return first;
}
template <typename It, typename T>
VAPOR_NODISCARD It LowerBound(It first, It last, const T &value) {
  return LowerBound(first, last, value, detail::LessOp());
}
// First element that is ordered after `value` (needs a sorted range).
template <typename It, typename T, typename Cmp>
VAPOR_NODISCARD It UpperBound(It first, It last, const T &value, Cmp cmp) {
  size_t len = static_cast<size_t>(last - first);
  while (len > 0) {
    size_t half = len / 2;
    It mid = first + half;
    if (!cmp(value, *mid)) {
      first = mid + 1;
      len -= half + 1;
    } else {
      len = half;
    }
  }
  return first;
}
template <typename It, typename T>
VAPOR_NODISCARD It UpperBound(It first, It last, const T &value) {
  return UpperBound(first, last, value, detail::LessOp());
}
template <typename It, typename T, typename Cmp>
VAPOR_NODISCARD bool BinarySearch(It first, It last, const T &value, Cmp cmp) {
  It it = LowerBound(first, last, value, cmp);
  return it != last && !cmp(value, *it);
}
template <typename It, typename T>
VAPOR_NODISCARD bool BinarySearch(It first, It last, const T &value) {
  return BinarySearch(first, last, value, detail::LessOp());
}

// ------------------------------------------------------------------ compare

template <typename It1, typename It2, typename Pred>
VAPOR_NODISCARD bool Equal(It1 first1, It1 last1, It2 first2, Pred pred) {
  for (; first1 != last1; ++first1, ++first2)
    if (!pred(*first1, *first2))
      return false;
  return true;
}
template <typename It1, typename It2>
VAPOR_NODISCARD bool Equal(It1 first1, It1 last1, It2 first2) {
  for (; first1 != last1; ++first1, ++first2)
    if (!(*first1 == *first2))
      return false;
  return true;
}
// Two-range form: also checks that the lengths match.
template <typename It1, typename It2>
VAPOR_NODISCARD bool Equal(It1 first1, It1 last1, It2 first2, It2 last2) {
  for (; first1 != last1 && first2 != last2; ++first1, ++first2)
    if (!(*first1 == *first2))
      return false;
  return first1 == last1 && first2 == last2;
}
template <typename It1, typename It2>
VAPOR_NODISCARD Pair<It1, It2> Mismatch(It1 first1, It1 last1, It2 first2) {
  while (first1 != last1 && *first1 == *first2) {
    ++first1;
    ++first2;
  }
  return Pair<It1, It2>(first1, first2);
}
template <typename It1, typename It2, typename Cmp>
VAPOR_NODISCARD bool LexicographicalCompare(It1 first1, It1 last1, It2 first2,
                                            It2 last2, Cmp cmp) {
  for (; first1 != last1 && first2 != last2; ++first1, ++first2) {
    if (cmp(*first1, *first2))
      return true;
    if (cmp(*first2, *first1))
      return false;
  }
  return first1 == last1 && first2 != last2;
}
template <typename It1, typename It2>
VAPOR_NODISCARD bool LexicographicalCompare(It1 first1, It1 last1, It2 first2,
                                            It2 last2) {
  return LexicographicalCompare(first1, last1, first2, last2, detail::LessOp());
}

template <typename It, typename Cmp>
VAPOR_NODISCARD bool IsSorted(It first, It last, Cmp cmp) {
  if (first == last)
    return true;
  for (It next = first; ++next != last; first = next)
    if (cmp(*next, *first))
      return false;
  return true;
}
template <typename It> VAPOR_NODISCARD bool IsSorted(It first, It last) {
  return IsSorted(first, last, detail::LessOp());
}

// ------------------------------------------------------------------- mutate

namespace detail {
template <typename From, typename To>
struct IsMemmovable
    : IntegralConstant<bool, IsPointer<From>::value && IsPointer<To>::value &&
                                 IsSame<RemoveCVT<RemovePointerT<From>>,
                                        RemoveCVT<RemovePointerT<To>>>::value &&
                                 !IsConst<RemovePointerT<To>>::value &&
                                 IsTriviallyCopyable<
                                     RemoveCVT<RemovePointerT<From>>>::value> {
};

// True when Fill can use memset: a pointer to a plain byte type and a value of
// that same type.
template <typename It, typename T>
struct IsByteFill
    : IntegralConstant<
          bool,
          IsPointer<It>::value && !IsConst<RemovePointerT<It>>::value &&
              IsSame<RemoveCVT<RemovePointerT<It>>, RemoveCVT<T>>::value &&
              (IsSame<RemoveCVT<T>, char>::value ||
               IsSame<RemoveCVT<T>, signed char>::value ||
               IsSame<RemoveCVT<T>, unsigned char>::value)> {};

template <typename It, typename Out>
inline Out CopyImpl(It first, It last, Out out, FalseType) {
  for (; first != last; ++first, ++out)
    *out = *first;
  return out;
}
template <typename T, typename U>
inline U *CopyImpl(T *first, T *last, U *out, TrueType) {
  size_t n = static_cast<size_t>(last - first);
  if (n)
    ::memmove(static_cast<void *>(out), static_cast<const void *>(first),
              n * sizeof(U));
  return out + n;
}

template <typename It, typename Out>
inline Out CopyBackwardImpl(It first, It last, Out out, FalseType) {
  while (first != last)
    *--out = *--last;
  return out;
}
template <typename T, typename U>
inline U *CopyBackwardImpl(T *first, T *last, U *out, TrueType) {
  size_t n = static_cast<size_t>(last - first);
  if (n)
    ::memmove(static_cast<void *>(out - n), static_cast<const void *>(first),
              n * sizeof(U));
  return out - n;
}

template <typename It, typename T>
inline void FillImpl(It first, It last, const T &value, FalseType) {
  for (; first != last; ++first)
    *first = value;
}
template <typename P, typename T>
inline void FillImpl(P *first, P *last, const T &value, TrueType) {
  ::memset(static_cast<void *>(first), static_cast<unsigned char>(value),
           static_cast<size_t>(last - first));
}
} // namespace detail

// Copies [first, last) to out (out may overlap the source only if it starts
// before `first`). Returns the end of the written range.
template <typename It, typename Out> Out Copy(It first, It last, Out out) {
  return detail::CopyImpl(first, last, out, detail::IsMemmovable<It, Out>());
}
// Like Copy, but fills the destination backwards from `out` (the end of the
// destination); safe when the destination overlaps and lies after the source.
template <typename It, typename Out>
Out CopyBackward(It first, It last, Out out) {
  return detail::CopyBackwardImpl(first, last, out,
                                  detail::IsMemmovable<It, Out>());
}
// Move-assigns each element (named MoveRange because Move() casts one value).
template <typename It, typename Out> Out MoveRange(It first, It last, Out out) {
  for (; first != last; ++first, ++out)
    *out = Move(*first);
  return out;
}
template <typename It, typename Out>
Out MoveBackward(It first, It last, Out out) {
  while (first != last)
    *--out = Move(*--last);
  return out;
}
template <typename It, typename T>
void Fill(It first, It last, const T &value) {
  detail::FillImpl(first, last, value,
                   IntegralConstant<bool, detail::IsByteFill<It, T>::value>());
}
template <typename It, typename Pred, typename T>
void ReplaceIf(It first, It last, Pred pred, const T &value) {
  for (; first != last; ++first)
    if (pred(*first))
      *first = value;
}
template <typename It1, typename It2>
It2 SwapRanges(It1 first1, It1 last1, It2 first2) {
  for (; first1 != last1; ++first1, ++first2)
    Swap(*first1, *first2);
  return first2;
}
template <typename It> void Reverse(It first, It last) {
  while (first != last) {
    --last;
    if (first == last)
      break;
    Swap(*first, *last);
    ++first;
  }
}
// Rotates so that `middle` becomes the first element. Returns where the old
// first element ended up.
template <typename It> It Rotate(It first, It middle, It last) {
  if (first == middle)
    return last;
  if (middle == last)
    return first;
  Reverse(first, middle);
  Reverse(middle, last);
  Reverse(first, last);
  It ret = first;
  for (It it = middle; it != last; ++it)
    ++ret;
  return ret;
}
// Removes elements by shifting the rest down; returns the new end. The tail
// elements are left in a valid but unspecified (moved-from) state.
template <typename It, typename Pred>
It RemoveIf(It first, It last, Pred pred) {
  first = FindIf(first, last, pred);
  if (first == last)
    return last;
  for (It it = first; ++it != last;)
    if (!pred(*it)) {
      *first = Move(*it);
      ++first;
    }
  return first;
}
template <typename It, typename T>
It Remove(It first, It last, const T &value) {
  first = Find(first, last, value);
  if (first == last)
    return last;
  for (It it = first; ++it != last;)
    if (!(*it == value)) {
      *first = Move(*it);
      ++first;
    }
  return first;
}
// Collapses runs of equal neighbours; returns the new end.
template <typename It, typename Eq> It Unique(It first, It last, Eq eq) {
  if (first == last)
    return last;
  It out = first;
  while (++first != last)
    if (!eq(*out, *first)) {
      ++out;
      if (out != first)
        *out = Move(*first);
    }
  return ++out;
}
template <typename It> It Unique(It first, It last) {
  return Unique(first, last,
                [](const detail::IterValueT<It> &a,
                   const detail::IterValueT<It> &b) { return a == b; });
}

// --------------------------------------------------------------------- heap
// A heap is an arrangement of a random-access range in which first[0] is the
// largest element (by `cmp`) and each element is not smaller than its two
// children first[2i+1] and first[2i+2]. It gives O(log n) insert and
// remove-largest, which is what a priority queue needs.

namespace detail {
// Places `value` into the hole at `idx`, moving larger children up.
template <typename It, typename V, typename Cmp>
void SiftDownHole(It first, size_t idx, size_t len, V &value, Cmp &cmp) {
  for (;;) {
    size_t child = 2 * idx + 1;
    if (child >= len)
      break;
    if (child + 1 < len && cmp(first[child], first[child + 1]))
      ++child;
    if (!cmp(value, first[child]))
      break;
    first[idx] = Move(first[child]);
    idx = child;
  }
  first[idx] = Move(value);
}

template <typename It, typename Cmp>
void MakeHeapImpl(It first, It last, Cmp &cmp) {
  typedef IterValueT<It> V;
  size_t n = static_cast<size_t>(last - first);
  for (size_t i = n / 2; i-- > 0;) {
    V v(Move(first[i]));
    SiftDownHole(first, i, n, v, cmp);
  }
}

template <typename It, typename Cmp>
void PushHeapImpl(It first, It last, Cmp &cmp) {
  typedef IterValueT<It> V;
  size_t idx = static_cast<size_t>(last - first);
  if (idx < 2)
    return;
  --idx;
  V v(Move(first[idx]));
  while (idx > 0) {
    size_t parent = (idx - 1) / 2;
    if (!cmp(first[parent], v))
      break;
    first[idx] = Move(first[parent]);
    idx = parent;
  }
  first[idx] = Move(v);
}

template <typename It, typename Cmp>
void PopHeapImpl(It first, It last, Cmp &cmp) {
  typedef IterValueT<It> V;
  size_t n = static_cast<size_t>(last - first);
  if (n < 2)
    return;
  V v(Move(*(last - 1)));
  *(last - 1) = Move(*first);
  SiftDownHole(first, 0, n - 1, v, cmp);
}

template <typename It, typename Cmp>
void SortHeapImpl(It first, It last, Cmp &cmp) {
  for (; last - first > 1; --last)
    PopHeapImpl(first, last, cmp);
}
} // namespace detail

// Rearranges the range into a heap.
template <typename It, typename Cmp> void MakeHeap(It first, It last, Cmp cmp) {
  detail::MakeHeapImpl(first, last, cmp);
}
template <typename It> void MakeHeap(It first, It last) {
  MakeHeap(first, last, detail::LessOp());
}
// [first, last - 1) must already be a heap; adds the element at last - 1.
template <typename It, typename Cmp> void PushHeap(It first, It last, Cmp cmp) {
  detail::PushHeapImpl(first, last, cmp);
}
template <typename It> void PushHeap(It first, It last) {
  PushHeap(first, last, detail::LessOp());
}
// Moves the largest element to last - 1 and re-heaps [first, last - 1).
template <typename It, typename Cmp> void PopHeap(It first, It last, Cmp cmp) {
  detail::PopHeapImpl(first, last, cmp);
}
template <typename It> void PopHeap(It first, It last) {
  PopHeap(first, last, detail::LessOp());
}
// Turns a heap into a sorted (ascending) range.
template <typename It, typename Cmp> void SortHeap(It first, It last, Cmp cmp) {
  detail::SortHeapImpl(first, last, cmp);
}
template <typename It> void SortHeap(It first, It last) {
  SortHeap(first, last, detail::LessOp());
}
// First position at which the heap property is violated (last if it holds).
template <typename It, typename Cmp>
VAPOR_NODISCARD It IsHeapUntil(It first, It last, Cmp cmp) {
  size_t n = static_cast<size_t>(last - first);
  for (size_t i = 1; i < n; ++i)
    if (cmp(first[(i - 1) / 2], first[i]))
      return first + i;
  return last;
}
template <typename It, typename Cmp>
VAPOR_NODISCARD bool IsHeap(It first, It last, Cmp cmp) {
  return IsHeapUntil(first, last, cmp) == last;
}
template <typename It> VAPOR_NODISCARD bool IsHeap(It first, It last) {
  return IsHeap(first, last, detail::LessOp());
}

// --------------------------------------------------------------------- sort

namespace detail {

template <typename It, typename Cmp>
void InsertionSort(It first, It last, Cmp &cmp) {
  typedef IterValueT<It> V;
  if (first == last)
    return;
  for (It i = first + 1; i != last; ++i) {
    if (cmp(*i, *first)) {
      V tmp(Move(*i));
      MoveBackward(first, i, i + 1);
      *first = Move(tmp);
    } else {
      V tmp(Move(*i));
      It j = i;
      while (cmp(tmp, *(j - 1))) {
        *j = Move(*(j - 1));
        --j;
      }
      *j = Move(tmp);
    }
  }
}

// Puts the median of *a, *b, *c into *result.
template <typename It, typename Cmp>
void MedianToFirst(It result, It a, It b, It c, Cmp &cmp) {
  if (cmp(*a, *b)) {
    if (cmp(*b, *c))
      Swap(*result, *b);
    else if (cmp(*a, *c))
      Swap(*result, *c);
    else
      Swap(*result, *a);
  } else if (cmp(*a, *c)) {
    Swap(*result, *a);
  } else if (cmp(*b, *c)) {
    Swap(*result, *c);
  } else {
    Swap(*result, *b);
  }
}

template <typename It, typename Cmp>
void IntroSort(It first, It last, size_t depth, Cmp &cmp) {
  const ptrdiff_t kSmall = 16;
  while (last - first > kSmall) {
    if (depth == 0) {
      MakeHeapImpl(first, last, cmp);
      SortHeapImpl(first, last, cmp);
      return;
    }
    --depth;
    It mid = first + (last - first) / 2;
    MedianToFirst(first, first + 1, mid, last - 1, cmp);
    // Partition (*first is the pivot): the median-of-three leaves a stopper
    // on both sides, so neither scan can run off the range.
    It lo = first + 1;
    It hi = last;
    for (;;) {
      while (cmp(*lo, *first))
        ++lo;
      --hi;
      while (cmp(*first, *hi))
        --hi;
      if (!(lo < hi))
        break;
      Swap(*lo, *hi);
      ++lo;
    }
    IntroSort(lo, last, depth, cmp);
    last = lo;
  }
  InsertionSort(first, last, cmp);
}

template <typename It, typename Cmp>
void MergeInPlace(It first, It middle, It last, size_t len1, size_t len2,
                  Cmp &cmp) {
  while (len1 != 0 && len2 != 0) {
    if (len1 + len2 == 2) {
      if (cmp(*middle, *first))
        Swap(*first, *middle);
      return;
    }
    It first_cut, second_cut;
    size_t len11, len22;
    if (len1 > len2) {
      len11 = len1 / 2;
      first_cut = first + len11;
      second_cut = LowerBound(middle, last, *first_cut, cmp);
      len22 = static_cast<size_t>(second_cut - middle);
    } else {
      len22 = len2 / 2;
      second_cut = middle + len22;
      first_cut = UpperBound(first, middle, *second_cut, cmp);
      len11 = static_cast<size_t>(first_cut - first);
    }
    It new_middle = Rotate(first_cut, middle, second_cut);
    MergeInPlace(first, first_cut, new_middle, len11, len22, cmp);
    // Continue with the right half iteratively to bound recursion depth.
    first = new_middle;
    middle = second_cut;
    len1 -= len11;
    len2 -= len22;
  }
}

template <typename It, typename Cmp>
void StableSortImpl(It first, It last, Cmp &cmp) {
  const ptrdiff_t kSmall = 8;
  if (last - first <= kSmall) {
    InsertionSort(first, last, cmp);
    return;
  }
  It mid = first + (last - first) / 2;
  StableSortImpl(first, mid, cmp);
  StableSortImpl(mid, last, cmp);
  MergeInPlace(first, mid, last, static_cast<size_t>(mid - first),
               static_cast<size_t>(last - mid), cmp);
}

inline size_t Log2Floor(size_t n) {
  size_t r = 0;
  while (n >>= 1)
    ++r;
  return r;
}
} // namespace detail

// Sorts [first, last) in ascending order (not stable).
template <typename It, typename Cmp> void Sort(It first, It last, Cmp cmp) {
  size_t n = static_cast<size_t>(last - first);
  if (n < 2)
    return;
  detail::IntroSort(first, last, 2 * detail::Log2Floor(n), cmp);
}
template <typename It> void Sort(It first, It last) {
  Sort(first, last, detail::LessOp());
}
// Sorts while keeping equal elements in their original order.
template <typename It, typename Cmp>
void StableSort(It first, It last, Cmp cmp) {
  if (last - first < 2)
    return;
  detail::StableSortImpl(first, last, cmp);
}
template <typename It> void StableSort(It first, It last) {
  StableSort(first, last, detail::LessOp());
}

// ------------------------------------------------- whole-container overloads
// `Sort(vec)` instead of `Sort(vec.begin(), vec.end())`. These work with any
// type that has begin()/end(): Vector, InlineVector, Array, Span, ...

#define VAPOR_RANGE_CALL0(call)                                                \
  ->decltype(call(c.begin(), c.end())) { return call(c.begin(), c.end()); }
#define VAPOR_RANGE_CALL1(call, x)                                             \
  ->decltype(call(c.begin(), c.end(), x)) {                                    \
    return call(c.begin(), c.end(), x);                                        \
  }
#define VAPOR_RANGE_CALL2(call, x, y)                                          \
  ->decltype(call(c.begin(), c.end(), x, y)) {                                 \
    return call(c.begin(), c.end(), x, y);                                     \
  }

template <typename C, typename T>
VAPOR_NODISCARD auto Find(C &c, const T &v)
    VAPOR_RANGE_CALL1(Find, v) template <typename C, typename Pred>
    VAPOR_NODISCARD auto FindIf(C &c, Pred p)
        VAPOR_RANGE_CALL1(FindIf, p) template <typename C, typename Pred>
        VAPOR_NODISCARD auto FindIfNot(C &c, Pred p)
            VAPOR_RANGE_CALL1(FindIfNot, p) template <typename C, typename T>
            VAPOR_NODISCARD auto Count(C &c, const T &v)
                VAPOR_RANGE_CALL1(Count, v) template <typename C, typename Pred>
                VAPOR_NODISCARD
    auto CountIf(C &c, Pred p)
        VAPOR_RANGE_CALL1(CountIf, p) template <typename C, typename Pred>
        VAPOR_NODISCARD auto AnyOf(C &c, Pred p)
            VAPOR_RANGE_CALL1(AnyOf, p) template <typename C, typename Pred>
            VAPOR_NODISCARD auto AllOf(C &c, Pred p)
                VAPOR_RANGE_CALL1(AllOf, p) template <typename C, typename Pred>
                VAPOR_NODISCARD
    auto NoneOf(C &c, Pred p)
        VAPOR_RANGE_CALL1(NoneOf, p) template <typename C, typename T>
        VAPOR_NODISCARD auto LowerBound(C &c, const T &v)
            VAPOR_RANGE_CALL1(LowerBound,
                              v) template <typename C, typename T, typename Cmp>
            VAPOR_NODISCARD
    auto LowerBound(C &c, const T &v, Cmp cmp)
        VAPOR_RANGE_CALL2(LowerBound, v, cmp) template <typename C, typename T>
        VAPOR_NODISCARD auto UpperBound(C &c, const T &v)
            VAPOR_RANGE_CALL1(UpperBound,
                              v) template <typename C, typename T, typename Cmp>
            VAPOR_NODISCARD
    auto UpperBound(C &c, const T &v, Cmp cmp)
        VAPOR_RANGE_CALL2(UpperBound, v, cmp) template <typename C, typename T>
        VAPOR_NODISCARD auto BinarySearch(C &c, const T &v)
            VAPOR_RANGE_CALL1(BinarySearch,
                              v) template <typename C, typename T, typename Cmp>
            VAPOR_NODISCARD auto BinarySearch(C &c, const T &v, Cmp cmp)
                VAPOR_RANGE_CALL2(BinarySearch, v,
                                  cmp) template <typename C, typename T>
                auto Fill(C &c, const T &v)
                    -> decltype(Fill(c.begin(), c.end(), v)) {
  Fill(c.begin(), c.end(), v);
}
template <typename C>
auto Reverse(C &c) -> decltype(Reverse(c.begin(), c.end())) {
  Reverse(c.begin(), c.end());
}
template <typename C> auto Sort(C &c) -> decltype(Sort(c.begin(), c.end())) {
  Sort(c.begin(), c.end());
}
template <typename C, typename Cmp>
auto Sort(C &c, Cmp cmp) -> decltype(Sort(c.begin(), c.end(), cmp)) {
  Sort(c.begin(), c.end(), cmp);
}
template <typename C>
auto StableSort(C &c) -> decltype(StableSort(c.begin(), c.end())) {
  StableSort(c.begin(), c.end());
}
template <typename C, typename Cmp>
auto StableSort(C &c, Cmp cmp)
    -> decltype(StableSort(c.begin(), c.end(), cmp)) {
  StableSort(c.begin(), c.end(), cmp);
}
template <typename C>
auto MakeHeap(C &c) -> decltype(MakeHeap(c.begin(), c.end())) {
  MakeHeap(c.begin(), c.end());
}
template <typename C, typename Cmp>
auto MakeHeap(C &c, Cmp cmp) -> decltype(MakeHeap(c.begin(), c.end(), cmp)) {
  MakeHeap(c.begin(), c.end(), cmp);
}
template <typename C>
auto PushHeap(C &c) -> decltype(PushHeap(c.begin(), c.end())) {
  PushHeap(c.begin(), c.end());
}
template <typename C, typename Cmp>
auto PushHeap(C &c, Cmp cmp) -> decltype(PushHeap(c.begin(), c.end(), cmp)) {
  PushHeap(c.begin(), c.end(), cmp);
}
template <typename C>
auto PopHeap(C &c) -> decltype(PopHeap(c.begin(), c.end())) {
  PopHeap(c.begin(), c.end());
}
template <typename C, typename Cmp>
auto PopHeap(C &c, Cmp cmp) -> decltype(PopHeap(c.begin(), c.end(), cmp)) {
  PopHeap(c.begin(), c.end(), cmp);
}
template <typename C>
auto SortHeap(C &c) -> decltype(SortHeap(c.begin(), c.end())) {
  SortHeap(c.begin(), c.end());
}
template <typename C, typename Cmp>
auto SortHeap(C &c, Cmp cmp) -> decltype(SortHeap(c.begin(), c.end(), cmp)) {
  SortHeap(c.begin(), c.end(), cmp);
}
template <typename C>
VAPOR_NODISCARD auto IsHeap(C &c)
    VAPOR_RANGE_CALL0(IsHeap) template <typename C, typename Cmp>
    VAPOR_NODISCARD auto IsHeap(C &c, Cmp cmp)
        VAPOR_RANGE_CALL1(IsHeap, cmp) template <typename C>
        VAPOR_NODISCARD auto IsSorted(C &c)
            VAPOR_RANGE_CALL0(IsSorted) template <typename C, typename Cmp>
            VAPOR_NODISCARD auto IsSorted(C &c, Cmp cmp)
                VAPOR_RANGE_CALL1(IsSorted, cmp) template <typename C>
                VAPOR_NODISCARD
    auto MinElement(C &c) VAPOR_RANGE_CALL0(MinElement) template <typename C>
    VAPOR_NODISCARD auto MaxElement(C &c)
        VAPOR_RANGE_CALL0(MaxElement) template <typename A, typename B>
        VAPOR_NODISCARD auto Equal(const A &a, const B &b)
            -> decltype(Equal(a.begin(), a.end(), b.begin(), b.end())) {
  return Equal(a.begin(), a.end(), b.begin(), b.end());
}

#undef VAPOR_RANGE_CALL0
#undef VAPOR_RANGE_CALL1
#undef VAPOR_RANGE_CALL2

} // namespace vapor
