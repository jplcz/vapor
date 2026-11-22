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

## Containers

Heap and exception-free replacements for common STL containers. They keep STL names
(`size`, `push_back`, `begin`, `value_or`, ...) so they are familiar, but any operation
that could throw in the STL (out of memory, capacity exceeded, empty value) instead
returns an `Expected<..., CommonError>` that you must check.

| Type | Use it when |
| --- | --- |
| `Array<T, N>` | Size is known at compile time; plain aggregate, no allocation. |
| `InlineVector<T, N>` | Size varies but has a fixed upper bound; storage lives inside the object, no allocation, copyable. Overflow returns `CapacityExceeded`. |
| `Vector<T>` | Size is unbounded; memory comes from a `vapor::Allocator`. Move-only; use `Clone()` for a fallible deep copy. |
| `Optional<T>` | A value that may be absent (`NullOpt` means empty). |
| `UniquePtr<T>` / `UniquePtr<T[]>` | Single ownership of an allocator-backed object; `TryAllocateUnique`/`TryMakeUnique` return `Expected`. |

`at()` aborts on a bad index (no exceptions exist to throw); use `checked_at()` to get
an `Expected` instead. `Vector`, `InlineVector` and `Array` convert implicitly to `Span`.

## Pair, Tuple, FunctionRef, algorithms and Bitset

Small building blocks that avoid pulling in `<utility>`, `<tuple>`, `<algorithm>` or
`<bitset>`. Names follow the STL in PascalCase; nothing allocates or throws.

* **`Pair<A, B>` / `Tuple<Ts...>`** bundle values so a function can return several results
  without inventing a struct. `Get<0>(t)` reads by position, `Get<float>(t)` by type (the type
  must be unique), and `Tie(a, b) = MakePair(1, 2)` unpacks into existing variables.
  Empty element types take no space. With C++17 you can write `auto [k, v] = pair;`.
* **`FunctionRef<R(Args...)>`** is a non-owning view of a callable: two machine words, never
  allocates, safe to pass by value. Use it for callbacks that only run while the callee
  executes (visitors, comparators). Unlike `Function` it does not keep the callable alive, so
  binding a temporary lambda is a compile error; give the lambda a name first.
* **Algorithms** (`algorithm.hpp`): `Sort`, `StableSort`, `Find`, `LowerBound`, `BinarySearch`,
  `Copy`, `Fill`, `Reverse`, `Rotate`, `Remove`, `Unique`, `Equal`, `Min`/`Max`/`Clamp`, and more.
  Pass an iterator pair or a whole container: `Sort(vec)`. `Sort` is introsort (worst case
  O(n log n), no allocation); `StableSort` keeps equal elements in order, needs no extra memory,
  and takes O(n log^2 n) time. Binary searches and sorts need random-access iterators.
* **`Bitset<N>`** stores N flags in 64-bit words (for example "which slots are in use").
  Combine sets with `&`, `|`, `^`, `~`, shift them, and walk set bits with `FindFirst()` /
  `FindNext(pos)`. Positions at or past `N` abort; `checked_test(pos)` returns an `Expected`
  instead.
