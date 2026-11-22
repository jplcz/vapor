#pragma once
#include <stddef.h>
#include <vapor/config.hpp>

// Placement new (`new (ptr) T(args)`) constructs an object in memory you
// already own. The compiler needs these operator declarations; by default they
// are provided here so vapor does not depend on any C++ standard library
// header. Define VAPOR_USE_STL=1 to use <new> instead.
#if defined(VAPOR_USE_STL)
#include <new>
#else
#ifndef VAPOR_PLACEMENT_NEW_DEFINED
#define VAPOR_PLACEMENT_NEW_DEFINED
inline void *operator new(size_t, void *p) noexcept { return p; }
inline void *operator new[](size_t, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
inline void operator delete[](void *, void *) noexcept {}
#endif
#endif
