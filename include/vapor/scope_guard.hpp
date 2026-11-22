#pragma once
#include <vapor/config.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// ScopeGuard: runs a function when the surrounding scope is left.
//
// Why: cleanup code (closing a handle, unlocking, undoing a half-finished
// change) must run on every exit path: early `return`, error branches and the
// normal end. Writing it once next to the acquisition, instead of repeating it
// before every `return`, prevents leaks. This is the same idea as Go's `defer`.
//
// How:
//
//   void *h = Open();
//   VAPOR_DEFER { Close(h); };          // runs when the scope ends
//
//   auto guard = MakeScopeGuard([&] { Rollback(); });
//   if (!Step1()) return;               // guard runs Rollback()
//   guard.Dismiss();                    // success: do not roll back
//
// The function is called at most once, in reverse order of declaration, and
// must not throw (the library is exception-free). A guard cannot be copied;
// moving it transfers the duty to run the function.

namespace vapor {

template <typename F> class ScopeGuard {
  F m_func;
  bool m_active;

public:
  explicit ScopeGuard(F f) : m_func(Move(f)), m_active(true) {}

  ScopeGuard(ScopeGuard &&o) noexcept(IsNothrowMoveConstructible<F>::value)
      : m_func(Move(o.m_func)), m_active(o.m_active) {
    o.m_active = false;
  }

  ScopeGuard(const ScopeGuard &) = delete;
  ScopeGuard &operator=(const ScopeGuard &) = delete;
  ScopeGuard &operator=(ScopeGuard &&) = delete;

  ~ScopeGuard() {
    if (m_active)
      m_func();
  }

  // Cancels the guard: the function will not run.
  void Dismiss() noexcept { m_active = false; }

  // Runs the function now (once) instead of at scope exit.
  void Run() {
    if (m_active) {
      m_active = false;
      m_func();
    }
  }

  VAPOR_NODISCARD bool IsActive() const noexcept { return m_active; }
};

template <typename F>
VAPOR_NODISCARD ScopeGuard<DecayT<F>> MakeScopeGuard(F &&f) {
  return ScopeGuard<DecayT<F>>(Forward<F>(f));
}

namespace detail {
struct ScopeGuardTag {};
template <typename F>
ScopeGuard<DecayT<F>> operator+(ScopeGuardTag, F &&f) {
  return ScopeGuard<DecayT<F>>(Forward<F>(f));
}
} // namespace detail

} // namespace vapor

#define VAPOR_DETAIL_CONCAT2(a, b) a##b
#define VAPOR_DETAIL_CONCAT(a, b) VAPOR_DETAIL_CONCAT2(a, b)

// VAPOR_DEFER { code; };  runs `code` at the end of the enclosing scope.
// It captures everything by reference, so locals declared before it are usable.
#define VAPOR_DEFER                                                            \
  auto VAPOR_DETAIL_CONCAT(vapor_defer_, __LINE__) =                           \
      ::vapor::detail::ScopeGuardTag() + [&]()
