#pragma once
#include <assert.h>
#include <vapor/algorithm.hpp>
#include <vapor/common.hpp>
#include <vapor/comparators.hpp>
#include <vapor/expected.hpp>
#include <vapor/vector.hpp>

// PriorityQueue: a queue that always hands out its "largest" element first.
//
// Why: schedulers, timers, shortest-path searches and "process the most
// urgent item next" loops need O(log n) insert and O(1) peek of the best
// element, without sorting everything after each insert.
//
// How: `PriorityQueue<int> q; (void)q.push(3); q.top(); q.pop();`
// Largest first by default (like std::priority_queue); pass `Greater<int>` as
// the second template argument for a smallest-first queue. `push`/`emplace`
// allocate, so they return Expected and can fail with OutOfMemory.
// `top()`/`pop()` assert the queue is not empty; `checked_top()` reports the
// error as a value instead.

namespace vapor {

template <typename T, typename Cmp = Less<T>> class PriorityQueue {
public:
  typedef T value_type;
  typedef size_t size_type;

  explicit PriorityQueue(Allocator &alloc = DefaultAllocator::GetDefault(),
                         Cmp cmp = Cmp()) noexcept
      : items_(alloc), cmp_(cmp) {}

  VAPOR_NODISCARD bool empty() const noexcept { return items_.empty(); }
  VAPOR_NODISCARD size_type size() const noexcept { return items_.size(); }

  VAPOR_NODISCARD const T &top() const noexcept {
    assert(!items_.empty());
    return items_[0];
  }
  VAPOR_NODISCARD Expected<ReferenceWrapper<const T>, CommonError>
  checked_top() const noexcept {
    if (items_.empty())
      return MakeUnexpected(CommonError::OutOfBounds);
    return ReferenceWrapper<const T>(items_[0]);
  }

  Expected<void, CommonError> push(const T &value) {
    auto r = items_.push_back(value);
    if (!r)
      return r;
    PushHeap(items_.begin(), items_.end(), cmp_);
    return Expected<void, CommonError>();
  }
  Expected<void, CommonError> push(T &&value) {
    auto r = items_.push_back(Move(value));
    if (!r)
      return r;
    PushHeap(items_.begin(), items_.end(), cmp_);
    return Expected<void, CommonError>();
  }
  template <typename... Args>
  Expected<void, CommonError> emplace(Args &&...args) {
    auto r = items_.emplace_back(Forward<Args>(args)...);
    if (!r)
      return MakeUnexpected(r.error());
    PushHeap(items_.begin(), items_.end(), cmp_);
    return Expected<void, CommonError>();
  }

  // Removes the top element.
  void pop() noexcept {
    assert(!items_.empty());
    PopHeap(items_.begin(), items_.end(), cmp_);
    items_.pop_back();
  }
  void clear() noexcept { items_.clear(); }
  void swap(PriorityQueue &o) noexcept {
    items_.swap(o.items_);
    Swap(cmp_, o.cmp_);
  }

private:
  Vector<T> items_;
  Cmp cmp_;
};

} // namespace vapor
