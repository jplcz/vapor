# vapor 🌫️

A lightweight, dependency-free, zero-allocation C++ utility library designed for environments where the 
Standard Template Library (STL) is unavailable, restricted, or unwanted (e.g., embedded systems, kernel-space, 
bare-metal development, or ultra-low-latency game engines).

## Key Philosophies

* **Zero Allocation:** No hidden heap management or performance penalties.
* **Header-Only:** Instantly integrates into any pipeline without pre-compilation dependencies.
* **C++11 Baseline Compatibility:** Attempts to fall back to basic language primitives on older toolchains
* **No STL by default:** vapor includes no C++ standard library header (only C headers such as
  `<stddef.h>`), so it works with `-nostdinc++`, in kernels and on bare metal. CI enforces this by
  compiling every header on its own with `-nostdinc++`.
* **Optional STL interoperability (opt-in):** define `VAPOR_USE_STL=1` (CMake: `-DVAPOR_USE_STL=ON`)
  to get conversion constructors and operators to native standard types (`std::string_view`,
  `std::span`, `std::optional`, `std::expected`, `std::hash`) in modern C++ environments. Without it
  those APIs simply do not exist, so nothing from the STL leaks in by accident.
* **Strict warnings:** vapor's own tests and header checks build with `-Wall -Wextra -Wpedantic
  -Wconversion -Wshadow -Wsign-conversion -Werror` (`/W4` on MSVC, reported but not fatal). Turn this off with
  `-DVAPOR_WARNINGS_AS_ERRORS=OFF`, for example when testing with a compiler newer than the ones we track.
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

## Bits, hashing, iterators, heaps, atomics and variants

More STL-free building blocks. All of them allocate nothing except `PriorityQueue`, whose
`push`/`emplace` can fail and so return an `Expected` (like `Vector`).

* **Bit utilities** (`bit.hpp`): `PopCount` (how many bits are 1), `CountTrailingZeros`,
  `CountLeadingZeros`, `BitWidth`, `IsPowerOfTwo`, `BitFloor`/`BitCeil` (round down/up to a power
  of two; `BitCeil` returns 0 on overflow instead of being undefined), `RotateLeft`/`RotateRight`,
  `ByteSwap` (flip endianness, for example when reading network data) and `BitCast<To>(from)`
  (reinterpret the bytes of one type as another without undefined behaviour). They map to CPU
  instructions through compiler builtins and fall back to portable code elsewhere.
* **`Hash<T>` and comparators** (`hash.hpp`, `comparators.hpp`): `Hash` turns a key into a
  `size_t` that a hash table can use to pick a bucket. Provided for integers, enums, floating
  point, pointers, `StringView` and `String`; specialise it for your own key types and combine
  fields with `HashCombine`. These hashes spread bits well but are **not cryptographic**: never
  use them where an attacker chooses the keys. `Less`, `Greater`, `EqualTo`, ... are the usual
  comparison function objects; `Less<>` compares mixed types without conversions.
* **Iterator helpers** (`iterator.hpp`): `Distance`, `Advance`, `Next`, `Prev`, plus
  `for (auto &x : Reversed(vec))` to walk backwards and `for (auto e : Enumerate(vec))` to get
  `e.index` and `e.value` in one loop. They keep a reference to the container, so do not pass a
  temporary.
* **`BitView`** (`bit_view.hpp`) treats a byte buffer you already own (a flags field, a
  protocol header, a hardware register image) as an array of bits without copying it. Bit `i` is
  bit `i % 8` (from the least significant) of byte `i / 8` on every platform. `ConstBitView` is the
  read-only form. Use `Bitset<N>` instead when you want an object that owns its bits.
* **Heap algorithms and `PriorityQueue`** (`algorithm.hpp`, `priority_queue.hpp`): a heap keeps
  the "largest" element at the front of an array, so inserting and removing the best element is
  O(log n). `MakeHeap`, `PushHeap`, `PopHeap`, `SortHeap`, `IsHeap` work on any random-access
  range. `PriorityQueue<T>` wraps a `Vector` for the common case: `push` / `top` / `pop`, largest
  first, or `PriorityQueue<T, Greater<T>>` for smallest first (schedulers, timers, shortest-path
  searches). `top()` and `pop()` abort on an empty queue; `checked_top()` returns an `Expected`.
* **`Atomic<T>`** (`atomic.hpp`) is a variable that several threads may use without a data race.
  `counter.fetch_add(1)`, `compare_exchange_strong(expected, desired)` (store only if the value
  is still what you expect; otherwise `expected` is updated) and `load`/`store` take a
  `MemoryOrder`. The default, `SeqCst`, is the safest; use `Acquire`/`Release` to pass data
  between threads and `Relaxed` for plain counters. GCC and Clang use `__atomic_*` builtins.
  MSVC uses `_Interlocked*` (4- and 8-byte types, always full ordering) and has not been tested.
* **`Variant<A, B, ...>`** (`variant.hpp`) holds exactly one value out of several types and
  remembers which, so protocols, parsers and state machines need no hand-written tagged union.
  Read it with `GetIf<T>(&v)` (a pointer, or `nullptr`), `Get<T>(v)` (aborts if another
  alternative is active, instead of throwing) or `Visit(visitor, v)`. Because there are no
  exceptions a `Variant` is never "valueless": it starts as the first alternative, so put
  `Monostate` first when that type has no default constructor.

## Dictionaries, shared ownership, strings and algorithms (round 3)

Added on top of the building blocks above. Same rules everywhere: no exceptions, anything that
may allocate returns an `Expected`, and copies are explicit (`Clone()`).

* **`HashMap<K, V>` / `HashSet<K>`** (`hash_map.hpp`, `hash_set.hpp`) find a value by key in
  constant average time, for caches, id-to-object lookups and "have I seen this?" checks.
  Use `try_emplace(key, args...)`, `find(key)` (compare with `end()`), `contains`, `at` (aborts if
  missing), `checked_at` (returns an `Expected`), `insert_or_assign`, `erase`. There is no
  `operator[]` because it would have to insert, which can fail. Inserting may rebuild the table
  and invalidates all iterators; iteration order is unspecified. A failed insert leaves the map
  unchanged. For lookups by a different key type (a `StringView` in a `String` map) use
  `HashMap<String, V, Hash<>, EqualTo<>>`. Internally it is open addressing (all entries in one
  array, collisions go to the next free slot) with tombstones for erased entries.
* **`FlatMap<K, V, Cmp>` / `FlatSet<K, Cmp>`** (`flat_map.hpp`, `flat_set.hpp`) keep entries in
  one sorted array: ordered iteration, `lower_bound`/`upper_bound`/`equal_range`, O(log n)
  lookup, very cache friendly, but O(n) insert and erase in the middle. Pick them for small or
  read-mostly maps; pick `HashMap` for large, write-heavy ones.
* **`SharedPtr<T>` / `WeakPtr<T>`** (`shared_ptr.hpp`) share one object between several owners;
  it is destroyed when the last `SharedPtr` goes away. A `WeakPtr` observes without keeping it
  alive (`lock()` returns a `SharedPtr`, empty if the object is gone), which breaks ownership
  cycles such as parent and child. Reference counts are atomic, so copies may be handed to
  other threads (the pointed-to object itself is not made thread-safe). Create with
  `TryMakeShared<T>(args...)` (one allocation for object and counters). No
  `enable_shared_from_this` and no dynamic cast.
* **`ScopeGuard` / `VAPOR_DEFER`** (`scope_guard.hpp`) run code when a scope ends, however it
  ends (return, break): `VAPOR_DEFER { close(fd); };`. `Dismiss()` cancels it.
* **`SmallString<N>`** (`small_string.hpp`) is a string with fixed inline capacity: no
  allocation, copyable, returns `CapacityExceeded` when full. Use for short names and labels.
* **`StringBuilder`** (`string_builder.hpp`) builds text from pieces (`Append` for strings,
  chars, bools, integers, doubles; `AppendHex`, `AppendJoin`, `AppendAll(a, b, c)`), then
  `view()`, `c_str()` or `Take()` the finished `String`.
* **String helpers** (`string_util.hpp`): `Trim`, `Split` (lazy, no allocation),
  `ConsumePrefix`/`ConsumeSuffix`, `EqualsIgnoreCase` (ASCII only), and strict parsers
  `ParseInt<T>(text, base)` (rejects junk and overflow with `InvalidArgument` /
  `ValueOutOfRange`), `ParseBool`, `ParseDouble`.
* **More algorithms**: `Accumulate`, `Transform`, `Iota`, `Partition`/`StablePartition`,
  `PartialSort`, `NthElement` (the n-th smallest without full sorting), `Merge`/`InplaceMerge`,
  `SetUnion`/`SetIntersection`/`SetDifference` on sorted ranges, `NextPermutation`, and more.

## Building and testing

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The tests are built for C++11, 14, 17, 23 and 26. On GCC and Clang they use
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wsign-conversion -Werror`. A separate
`header_check_cppNN` target compiles every public header on its own with `-nostdinc++`, which
proves each header is self-contained and includes no C++ standard library header.
The tests themselves enable `VAPOR_USE_STL=1` to cover the optional interoperability layer.
