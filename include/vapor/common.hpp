#pragma once
#include <errno.h>
#include <vapor/config.hpp>

namespace vapor {
// STL-compatible: return a reference to one of the arguments, so do not keep
// the result past the lifetime of the arguments (e.g. when passing temporaries).
template <typename T>
VAPOR_NODISCARD constexpr const T &Min(const T &a, const T &b) {
  return b < a ? b : a;
}
template <typename T, typename Cmp>
VAPOR_NODISCARD constexpr const T &Min(const T &a, const T &b, Cmp cmp) {
  return cmp(b, a) ? b : a;
}
template <typename T>
VAPOR_NODISCARD constexpr const T &Max(const T &a, const T &b) {
  return a < b ? b : a;
}
template <typename T, typename Cmp>
VAPOR_NODISCARD constexpr const T &Max(const T &a, const T &b, Cmp cmp) {
  return cmp(a, b) ? b : a;
}

// Error codes shared by every vapor API.
//
// Where a POSIX errno value describes the failure, the enumerator has exactly
// that value (OutOfMemory == ENOMEM, InvalidArgument == EINVAL, ...). So an
// errno from the operating system converts with a plain cast, and a
// CommonError converts back to an errno the same way:
//
//   CommonError e = static_cast<CommonError>(errno);
//   int code = static_cast<int>(CommonError::WouldBlock);   // EAGAIN
//
// An errno that has no named enumerator is still a valid CommonError value
// (the underlying type is int); compare it against the named ones or print the
// number. Errors with no errno equivalent (EmptyString, EmptyPointer,
// EmptyOptional) use values from 1000 up, which no platform assigns to errno.
enum class CommonError : int {
  OutOfMemory = ENOMEM,
  OutOfBounds = EDOM,
  EmptyString = 1000,
  ValueNotFound = ENOENT,
  UnsupportedOperation = ENOTSUP,
  EmptyPointer = 1001,
  EmptyOptional = 1002,
  CapacityExceeded = ENOBUFS,
  InvalidArgument = EINVAL,
  ValueOutOfRange = ERANGE,
  IoError = EIO,
  WouldBlock = EAGAIN,
  AlreadyExists = EEXIST,
  PermissionDenied = EACCES,
  Interrupted = EINTR,
  BadDescriptor = EBADF,
  BrokenPipe = EPIPE,
  TimedOut = ETIMEDOUT,
  Busy = EBUSY
};

// Converts an errno value to a CommonError (same number, see above).
VAPOR_NODISCARD constexpr CommonError ErrorFromErrno(int err) noexcept {
  return static_cast<CommonError>(err);
}
// The errno-compatible number of a CommonError.
VAPOR_NODISCARD constexpr int ErrnoFromError(CommonError e) noexcept {
  return static_cast<int>(e);
}

} // namespace vapor
