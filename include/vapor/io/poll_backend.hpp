#pragma once

#include <vapor/allocator.hpp>
#include <vapor/io/fd.hpp>
#include <vapor/vector.hpp>

// PollBackend: the operating-system "which descriptors are ready?" mechanism
// behind IoScheduler.
//
// Why: Linux offers epoll, BSD and macOS offer kqueue, every POSIX system
// offers poll and select, and they differ in speed and limits. The scheduler
// only needs four operations, so each mechanism is hidden behind this small
// interface and the scheduler code stays identical for all of them.
//
// How: normally you do not touch this header; pick a backend with
// IoBackendKind when creating the scheduler (or leave it at Default, which
// follows the VAPOR_IO_BACKEND_* define, see io_backends.hpp). To support a
// mechanism vapor does not ship, derive from PollBackend and pass the object
// to IoScheduler::Create.
//
// Backends are level-triggered: a descriptor keeps being reported while it
// stays ready. Not thread-safe; the scheduler uses one from a single thread.

namespace vapor {

// Bit flags describing what a descriptor is interested in / what happened.
// Error and Hangup are always reported, even when not requested.
struct IoEvent {
  enum Flag : unsigned { Read = 1u, Write = 2u, Error = 4u, Hangup = 8u };
};

struct PollEvent {
  int fd;
  unsigned events; // IoEvent::Flag bits
};

class PollBackend {
public:
  virtual ~PollBackend() {}

  // True when Add/Modify/Remove may be called from another thread while a
  // thread is blocked inside Wait (epoll). Otherwise the scheduler first
  // interrupts the waiting thread.
  virtual bool ConcurrentMutationSafe() const noexcept { return false; }

  // Start watching `fd` for `events` (Read and/or Write).
  virtual Expected<void, CommonError> Add(int fd, unsigned events) noexcept = 0;
  // Change the interest of an already added descriptor.
  virtual Expected<void, CommonError> Modify(int fd,
                                             unsigned events) noexcept = 0;
  // Stop watching. Safe to call for descriptors that are already closed.
  virtual void Remove(int fd) noexcept = 0;
  // Waits up to `timeout_ms` (-1 = forever, 0 = do not block) and replaces the
  // contents of `out` with the ready descriptors. Interrupted waits (EINTR)
  // return an empty result rather than an error.
  virtual Expected<void, CommonError> Wait(int timeout_ms,
                                           Vector<PollEvent> &out) noexcept = 0;
};

} // namespace vapor
