#pragma once
#include <stdlib.h>
#include <vapor/allocator.hpp>

namespace vapor_test {

// Allocator that can be told to fail and that tracks live allocations.
class TestAllocator final : public vapor::Allocator {
public:
  bool fail = false;
  int live = 0;

  void *Allocate(size_t size, size_t) noexcept override {
    if (fail || size == 0)
      return nullptr;
    ++live;
    return ::malloc(size);
  }
  void *Reallocate(void *ptr, size_t size, size_t) noexcept override {
    if (fail)
      return nullptr;
    if (!ptr)
      ++live;
    return ::realloc(ptr, size);
  }
  void Deallocate(void *ptr, size_t) noexcept override {
    if (ptr)
      --live;
    ::free(ptr);
  }
};

// Counts constructions/destructions to catch leaks and double destruction.
template <int Tag> struct TrackedT {
  static int alive;
  static int copies;
  int value;
  explicit TrackedT(int v = 0) : value(v) { ++alive; }
  TrackedT(const TrackedT &o) : value(o.value) {
    ++alive;
    ++copies;
  }
  TrackedT(TrackedT &&o) noexcept : value(o.value) {
    ++alive;
    o.value = -1;
  }
  TrackedT &operator=(const TrackedT &o) {
    value = o.value;
    ++copies;
    return *this;
  }
  TrackedT &operator=(TrackedT &&o) noexcept {
    value = o.value;
    o.value = -1;
    return *this;
  }
  ~TrackedT() { --alive; }
  friend bool operator==(const TrackedT &a, const TrackedT &b) {
    return a.value == b.value;
  }
};
template <int Tag> int TrackedT<Tag>::alive = 0;
template <int Tag> int TrackedT<Tag>::copies = 0;
using Tracked = TrackedT<0>;

} // namespace vapor_test
