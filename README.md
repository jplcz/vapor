# vapor 🌫️

A lightweight, dependency-free, zero-allocation C++ utility library designed for environments where the 
Standard Template Library (STL) is unavailable, restricted, or unwanted (e.g., embedded systems, kernel-space, 
bare-metal development, or ultra-low-latency game engines).

## Key Philosophies

* **Zero Allocation:** No hidden heap management or performance penalties.
* **Header-Only:** Instantly integrates into any pipeline without pre-compilation dependencies.
* **C++11 Baseline Compatibility:** Attempts to fall back to basic language primitives on older toolchains
* **Modern STL Interoperability:** Automatically provides zero-overhead conversion constructors and operators to 
  native standard types (`std::string_view`, `std::span`, `std::expected`) when compiled in modern C++ environments.
* **Priority Overrides:** Gives your compiler definitions final say over CMake options via robust conditional preprocessing.

## Integration

### 1. Modern CMake (Recommended via FetchContent)

By default, when `vapor` detects it is being pulled into another project as a sub-module, it 
**automatically disables** internal installation layout generation and GoogleTest compilation hooks.

```cmake
include(FetchContent)

FetchContent_Declare(
    vapor
    GIT_REPOSITORY https://github.com/jplcz/vapor.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(vapor)

# Link directly against the interface target
target_link_libraries(your_project PRIVATE vapor::vapor)
```

## Generic Utilities (STL-free)

Templates often need to ask questions about types ("is `T` trivially copyable?") or move/forward
values. The STL answers these in `<type_traits>` and `<utility>`, which are not available in
freestanding or kernel builds. Vapor ships its own versions built only on language features and
compiler intrinsics (special `__is_*` keywords provided by GCC, Clang and MSVC).

* `vapor/type_traits.hpp` — `IsSame`, `EnableIf`, `Conditional`, `RemoveReference`, `RemoveCV`,
  `Decay`, `IsConvertible`, `IsNothrowMoveConstructible`, plus intrinsic-backed `IsTriviallyCopyable`,
  `IsTriviallyDestructible`, `IsTrivial`, `IsStandardLayout`, `IsPod`, `IsEmpty`, `IsAggregate`, `IsEnum`,
  `IsUnion`, `IsClass`, `IsPolymorphic`, `IsAbstract`, `IsFinal`, `IsBaseOf`, `IsTriviallyConstructible`,
  `IsNothrowConstructible`, `IsNothrowAssignable`, `UnderlyingType`, ...
  Intrinsic-backed traits only use builtins present in GCC 9, Clang and MSVC (the minimum supported compilers).
  Use `Trait<T>::value` / `Trait<T>::type`, or the `TraitT<T>` alias for the type.
* `vapor/utility.hpp` — `Move`, `Forward`, `Swap`, `Exchange`, `AddressOf`.
  `Move(x)` hands over ownership of `x`; `Forward<T>(x)` passes `x` on exactly as the caller
  supplied it (lvalue stays lvalue, rvalue stays rvalue) inside `template <typename T> f(T &&x)`.

```cpp
template <typename T, vapor::EnableIfT<vapor::IsTriviallyCopyable<T>::value, int> = 0>
void Copy(T *dst, const T *src, size_t n) { memcpy(dst, src, n * sizeof(T)); }
```
