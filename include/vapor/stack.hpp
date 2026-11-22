#pragma once
#include <stddef.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/utility.hpp>
#include <vapor/vector.hpp>

// Stack<T, Container>: a last-in-first-out (LIFO) adapter, like std::stack.
//
// Why: when code only ever pushes, looks at the newest element and pops it
// (undo history, depth-first search, expression parsing), saying `Stack`
// instead of `Vector` documents the intent and removes every operation that
// does not belong (no indexing, no insertion in the middle).
//
// How:
//
//   Stack<int> s;
//   s.push(1);               // Expected<void, ...>: may fail to allocate
//   s.top() = 2;             // newest element; aborts when empty
//   s.pop();                 // removes it
//   s.empty(); s.size();
//
// The second template argument is the underlying container (default
// Vector<T>); any container with push_back, emplace_back, pop_back, back,
// size, empty and swap works, for example Deque<T> or SmallVector<T, N>.
// Like the containers, copying is the explicit Clone(), which needs a
// container with Clone().

namespace vapor {

template <typename T, typename Container = Vector<T>> class Stack {
public:
  typedef Container container_type;
  typedef T value_type;
  typedef size_t size_type;
  typedef T &reference;
  typedef const T &const_reference;

private:
  Container m_c;

public:
  explicit Stack(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_c(alloc) {}
  explicit Stack(Container &&c) noexcept : m_c(Move(c)) {}
  Stack(Stack &&) = default;
  Stack &operator=(Stack &&) = default;
  Stack(const Stack &) = delete;
  Stack &operator=(const Stack &) = delete;

  VAPOR_NODISCARD Expected<Stack, CommonError> Clone() const {
    Expected<Container, CommonError> c = m_c.Clone();
    if (!c)
      return MakeUnexpected(c.error());
    return Stack(Move(c.value()));
  }

  VAPOR_NODISCARD bool empty() const noexcept { return m_c.empty(); }
  VAPOR_NODISCARD size_type size() const noexcept { return m_c.size(); }
  VAPOR_NODISCARD reference top() noexcept { return m_c.back(); }
  VAPOR_NODISCARD const_reference top() const noexcept { return m_c.back(); }

  Expected<void, CommonError> push(const T &v) { return m_c.push_back(v); }
  Expected<void, CommonError> push(T &&v) { return m_c.push_back(Move(v)); }
  template <typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace(Args &&...args) {
    return m_c.emplace_back(Forward<Args>(args)...);
  }
  void pop() noexcept { m_c.pop_back(); }
  void swap(Stack &o) noexcept { m_c.swap(o.m_c); }

  // Read access to the underlying container (oldest element first).
  VAPOR_NODISCARD const Container &container() const noexcept { return m_c; }
};

} // namespace vapor
