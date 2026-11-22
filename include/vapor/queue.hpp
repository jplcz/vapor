#pragma once
#include <stddef.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/deque.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/utility.hpp>

// Queue<T, Container>: a first-in-first-out (FIFO) adapter, like std::queue.
//
// Why: work lists, breadth-first search and message passing add at one end
// and take from the other. `Queue` exposes only that, so a stray middle
// insertion cannot slip in, and it is backed by a Deque so both ends are O(1).
//
// How:
//
//   Queue<int> q;
//   q.push(1);               // Expected<void, ...>: may fail to allocate
//   q.front();               // oldest element; aborts when empty
//   q.back();                // newest element
//   q.pop();                 // removes the oldest
//
// The second template argument is the underlying container (default
// Deque<T>); any container with push_back, emplace_back, pop_front, front,
// back, size, empty and swap works, for example List<T>. Copying is the
// explicit Clone().

namespace vapor {

template <typename T, typename Container = Deque<T>> class Queue {
public:
  typedef Container container_type;
  typedef T value_type;
  typedef size_t size_type;
  typedef T &reference;
  typedef const T &const_reference;

private:
  Container m_c;

public:
  explicit Queue(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_c(alloc) {}
  explicit Queue(Container &&c) noexcept : m_c(Move(c)) {}
  Queue(Queue &&) = default;
  Queue &operator=(Queue &&) = default;
  Queue(const Queue &) = delete;
  Queue &operator=(const Queue &) = delete;

  VAPOR_NODISCARD Expected<Queue, CommonError> Clone() const {
    Expected<Container, CommonError> c = m_c.Clone();
    if (!c)
      return MakeUnexpected(c.error());
    return Queue(Move(c.value()));
  }

  VAPOR_NODISCARD bool empty() const noexcept { return m_c.empty(); }
  VAPOR_NODISCARD size_type size() const noexcept { return m_c.size(); }
  VAPOR_NODISCARD reference front() noexcept { return m_c.front(); }
  VAPOR_NODISCARD const_reference front() const noexcept { return m_c.front(); }
  VAPOR_NODISCARD reference back() noexcept { return m_c.back(); }
  VAPOR_NODISCARD const_reference back() const noexcept { return m_c.back(); }

  Expected<void, CommonError> push(const T &v) { return m_c.push_back(v); }
  Expected<void, CommonError> push(T &&v) { return m_c.push_back(Move(v)); }
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace(Args &&...args) {
    return m_c.emplace_back(Forward<Args>(args)...);
  }
  void pop() noexcept { m_c.pop_front(); }
  void swap(Queue &o) noexcept { m_c.swap(o.m_c); }

  VAPOR_NODISCARD const Container &container() const noexcept { return m_c; }
};

} // namespace vapor
