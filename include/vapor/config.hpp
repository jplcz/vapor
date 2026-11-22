#pragma once
#include <stdint.h>

// vapor does not include any C++ standard library header by default.
// Define VAPOR_USE_STL=1 to opt in to interoperability with std::string,
// std::string_view, std::span, std::optional, std::expected and std::hash.
#if defined(VAPOR_NO_STL) || (defined(VAPOR_USE_STL) && VAPOR_USE_STL == 0)
#undef VAPOR_USE_STL
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

// [[nodiscard]] on a class/struct needs C++17 (or Clang's attribute).
#ifndef VAPOR_NODISCARD_TYPE
#if __cplusplus >= 201703L
#define VAPOR_NODISCARD_TYPE [[nodiscard]]
#elif defined(__clang__)
#define VAPOR_NODISCARD_TYPE __attribute__((warn_unused_result))
#else
#define VAPOR_NODISCARD_TYPE
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
