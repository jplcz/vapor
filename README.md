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
* 
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
