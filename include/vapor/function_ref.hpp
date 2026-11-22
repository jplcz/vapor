#pragma once
#include <stdlib.h>
#include <vapor/config.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// FunctionRef<R(Args...)>: a non-owning view of something callable.
//
// Why: `Function` (functional.hpp) owns its callable, which may need an
// allocation. When a callback is only used while the callee runs (a visitor, a
// comparator, a "for each" body), a view is cheaper, cannot fail, and is just
// two machine words that you can pass by value.
//
// How: `void ForEach(FunctionRef<void(int)> f); ... ForEach(lambda);`
// The lambda (or functor) must outlive the FunctionRef. For that reason a
// temporary callable cannot be bound: `FunctionRef<int()> r([] { return 1; });`
// does not compile because the lambda would be gone after the statement. Name
// the lambda first, or pass it directly as a function argument by lvalue.
// Plain function pointers are copied into the view and are always safe.
// Calling an empty FunctionRef aborts.

namespace vapor {

template <typename Sig> class FunctionRef;

namespace detail {

template <typename F, typename R, typename... Args>
auto FunctionRefCallTest(int)
    -> decltype((void)static_cast<R>(Declval<F>()(Declval<Args>()...)),
                TrueType());
template <typename F, typename R, typename... Args>
FalseType FunctionRefCallTest(...);

template <typename F, typename R, typename... Args>
struct IsCallableAs : decltype(FunctionRefCallTest<F, R, Args...>(0)) {};

template <typename T>
struct IsFunctionPointer
    : IntegralConstant<bool, IsPointer<T>::value &&
                                 IsFunction<RemovePointerT<T>>::value> {};

} // namespace detail

template <typename R, typename... Args> class FunctionRef<R(Args...)> {
  union Storage {
    void *obj;
    void (*fn)();
  };
  typedef R (*Trampoline)(Storage, Args...);

  Storage m_storage;
  Trampoline m_call;

  template <typename F> static R CallObject(Storage s, Args... args) {
    return static_cast<R>((*static_cast<F *>(s.obj))(Forward<Args>(args)...));
  }
  template <typename Fp> static R CallFunction(Storage s, Args... args) {
    return static_cast<R>(reinterpret_cast<Fp>(s.fn)(Forward<Args>(args)...));
  }

  template <typename F, typename Self = FunctionRef>
  using IsObject =
      IntegralConstant<bool, !IsSame<RemoveCVT<F>, Self>::value &&
                                 !detail::IsFunctionPointer<F>::value &&
                                 !IsFunction<F>::value>;

public:
  FunctionRef() noexcept : m_call(nullptr) { m_storage.obj = nullptr; }
  FunctionRef(decltype(nullptr)) noexcept : m_call(nullptr) {
    m_storage.obj = nullptr;
  }

  FunctionRef(const FunctionRef &) noexcept = default;
  FunctionRef &operator=(const FunctionRef &) noexcept = default;

  // Function (pointer): copied into the view.
  template <typename Fp,
            EnableIfT<detail::IsFunctionPointer<Fp>::value &&
                          detail::IsCallableAs<Fp, R, Args...>::value,
                      int> = 0>
  FunctionRef(Fp fp) noexcept {
    if (fp) {
      m_storage.fn = reinterpret_cast<void (*)()>(fp);
      m_call = &CallFunction<Fp>;
    } else {
      m_storage.obj = nullptr;
      m_call = nullptr;
    }
  }

  // Lvalue functor or lambda: referenced, must outlive this view.
  template <typename F,
            EnableIfT<IsObject<F>::value &&
                          detail::IsCallableAs<F &, R, Args...>::value,
                      int> = 0>
  FunctionRef(F &f) noexcept {
    m_storage.obj = const_cast<void *>(
        static_cast<const volatile void *>(AddressOf(f)));
    m_call = &CallObject<F>;
  }

  // Temporaries would dangle immediately.
  template <typename F,
            EnableIfT<!IsLvalueReference<F>::value && IsObject<F>::value, int> = 0>
  FunctionRef(F &&) = delete;

  explicit operator bool() const noexcept { return m_call != nullptr; }

  R operator()(Args... args) const {
    if (!m_call)
      abort();
    return m_call(m_storage, Forward<Args>(args)...);
  }
};

} // namespace vapor
