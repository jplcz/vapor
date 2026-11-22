#pragma once

#include <vapor/common.hpp>
#include <vapor/config.hpp>
#include <vapor/expected.hpp>
#include <vapor/pair.hpp>

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#define VAPOR_HAS_POSIX_IO 1
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#else
#define VAPOR_HAS_POSIX_IO 0
#endif

// Fd: an owning wrapper around an operating-system file descriptor.
//
// Why: file descriptors (small integers naming files, sockets, pipes...) must
// be closed exactly once. Forgetting leaks them, closing twice can close an
// unrelated descriptor that reused the number. Fd closes in its destructor and
// cannot be copied, so ownership is always clear.
//
// How:
//
//   auto p = Fd::MakePipe();                 // Expected<Pair<Fd, Fd>, ...>
//   Fd &read_end = p.value().first;
//   (void)read_end.SetNonBlocking(true);     // read() returns EAGAIN, not wait
//   int raw = read_end.get();                // borrow for system calls
//   int owned = read_end.release();          // give up ownership
//
// Failures return Expected with a CommonError whose number is the errno of the
// failed call (see common.hpp). On platforms without POSIX I/O every factory
// returns UnsupportedOperation. This header and the rest of vapor/io/ are the
// only places that include operating-system headers.

namespace vapor {

class Fd {
  int m_fd;

public:
  Fd() noexcept : m_fd(-1) {}
  explicit Fd(int fd) noexcept : m_fd(fd) {}
  Fd(Fd &&o) noexcept : m_fd(o.m_fd) { o.m_fd = -1; }
  Fd &operator=(Fd &&o) noexcept {
    if (this != &o) {
      reset(o.m_fd);
      o.m_fd = -1;
    }
    return *this;
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  ~Fd() { reset(); }

  VAPOR_NODISCARD int get() const noexcept { return m_fd; }
  VAPOR_NODISCARD bool valid() const noexcept { return m_fd >= 0; }

  // Gives up ownership without closing.
  VAPOR_NODISCARD int release() noexcept {
    int f = m_fd;
    m_fd = -1;
    return f;
  }

  // Closes the current descriptor (if any) and takes ownership of `fd`.
  void reset(int fd = -1) noexcept {
#if VAPOR_HAS_POSIX_IO
    if (m_fd >= 0 && m_fd != fd)
      ::close(m_fd);
#endif
    m_fd = fd;
  }

#if VAPOR_HAS_POSIX_IO
  Expected<void, CommonError> SetNonBlocking(bool on) noexcept {
    return SetFlag(F_GETFL, F_SETFL, O_NONBLOCK, on);
  }
  Expected<void, CommonError> SetCloseOnExec(bool on) noexcept {
    return SetFlag(F_GETFD, F_SETFD, FD_CLOEXEC, on);
  }

  // New descriptor referring to the same open file; close-on-exec is set.
  VAPOR_NODISCARD Expected<Fd, CommonError> Duplicate() const noexcept {
    if (m_fd < 0)
      return MakeUnexpected(CommonError::BadDescriptor);
    int n = ::fcntl(m_fd, F_DUPFD_CLOEXEC, 0);
    if (n < 0)
      return MakeUnexpected(ErrorFromErrno(errno));
    return Fd(n);
  }

  // Creates a pipe: bytes written to `second` are read from `first`. Both ends
  // are non-blocking and close-on-exec.
  static Expected<Pair<Fd, Fd>, CommonError> MakePipe() noexcept {
    int fds[2];
    if (::pipe(fds) != 0)
      return MakeUnexpected(ErrorFromErrno(errno));
    Fd rd(fds[0]);
    Fd wr(fds[1]);
    Pair<Fd, Fd> p(Move(rd), Move(wr));
    Expected<void, CommonError> r = p.first.SetNonBlocking(true);
    if (r)
      r = p.first.SetCloseOnExec(true);
    if (r)
      r = p.second.SetNonBlocking(true);
    if (r)
      r = p.second.SetCloseOnExec(true);
    if (!r)
      return MakeUnexpected(r.error());
    return Move(p);
  }

private:
  Expected<void, CommonError> SetFlag(int get, int set, int flag,
                                      bool on) noexcept {
    if (m_fd < 0)
      return MakeUnexpected(CommonError::BadDescriptor);
    int cur = ::fcntl(m_fd, get);
    if (cur < 0)
      return MakeUnexpected(ErrorFromErrno(errno));
    int want = on ? (cur | flag) : (cur & ~flag);
    if (want != cur && ::fcntl(m_fd, set, want) < 0)
      return MakeUnexpected(ErrorFromErrno(errno));
    return {};
  }
#else
  Expected<void, CommonError> SetNonBlocking(bool) noexcept {
    return MakeUnexpected(CommonError::UnsupportedOperation);
  }
  Expected<void, CommonError> SetCloseOnExec(bool) noexcept {
    return MakeUnexpected(CommonError::UnsupportedOperation);
  }
  VAPOR_NODISCARD Expected<Fd, CommonError> Duplicate() const noexcept {
    return MakeUnexpected(CommonError::UnsupportedOperation);
  }
  static Expected<Pair<Fd, Fd>, CommonError> MakePipe() noexcept {
    return MakeUnexpected(CommonError::UnsupportedOperation);
  }
#endif
};

} // namespace vapor
