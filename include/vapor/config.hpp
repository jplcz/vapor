#pragma once
#include <stdint.h>

// Allow explicit opt-out via VAPOR_NO_STL or VAPOR_USE_STL=0
#if defined(VAPOR_NO_STL) || (defined(VAPOR_USE_STL) && VAPOR_USE_STL == 0)
#undef VAPOR_USE_STL
// Otherwise, if VAPOR_USE_STL isn't explicitly defined, default it to ON
#elif !defined(VAPOR_USE_STL)
#define VAPOR_USE_STL 1
#endif

#ifndef VAPOR_NODISCARD
#if __cplusplus >= 201703L
#define VAPOR_NODISCARD [[nodiscard]]
#elif defined(__clang__) || defined(__GNUC__)
#define VAPOR_NODISCARD __attribute__((warn_unused_result))
#else
#define VAPOR_NODISCARD // Fallback to nothing on older MSVC/other compilers
#endif
#endif

#if __cplusplus >= 201402L
#define VAPOR_CXX14_CONSTEXPR constexpr
#else
#define VAPOR_CXX14_CONSTEXPR
#endif

#if __cplusplus >= 201703L
#define VAPOR_CXX17_CONSTEXPR constexpr
#else
#define VAPOR_CXX17_CONSTEXPR
#endif

#if __cplusplus >= 202002L
#define VAPOR_CXX20_CONSTEXPR constexpr
#else
#define VAPOR_CXX20_CONSTEXPR
#endif

#ifndef VAPOR_SIZE_MAX
#if defined(SIZE_MAX)
#define VAPOR_SIZE_MAX SIZE_MAX
#else
// Ultimate fallback if an obscure compiler breaks specification rules
#define VAPOR_SIZE_MAX (~(size_t)0)
#endif
#endif

#if !defined(VAPOR_OVERRIDE_FEATURES)
#if defined(__cplusplus) && (__cplusplus >= 202002L)
// If the user compiles with -std=c++20 or higher, default to Concepts
#define VAPOR_USE_CONCEPTS 1
#elif defined(__cplusplus) && (__cplusplus >= 201103L)
// If compiling with C++11/C++14/C++17, default to Type Traits
#define VAPOR_USE_TYPE_TRAITS 1
#endif
#endif

#if defined(_MSC_VER) // Microsoft Visual Studio Compiler
#define VAPOR_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__) // GCC and Clang Engines
#define VAPOR_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define VAPOR_ALWAYS_INLINE inline // Fallback rule for generic compilers
#endif
