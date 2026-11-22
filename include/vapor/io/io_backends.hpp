#pragma once

#include <vapor/io/poll_backend.hpp>

#if VAPOR_HAS_POSIX_IO

#include <poll.h>
#include <sys/select.h>

#if defined(__linux__)
#include <sys/epoll.h>
#define VAPOR_IO_HAS_EPOLL 1
#else
#define VAPOR_IO_HAS_EPOLL 0
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||       \
    defined(__OpenBSD__) || defined(__DragonFly__)
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#define VAPOR_IO_HAS_KQUEUE 1
#else
#define VAPOR_IO_HAS_KQUEUE 0
#endif

// The concrete PollBackend implementations.
//
// Which one is "Default" is chosen at compile time with one of
//   -DVAPOR_IO_BACKEND_EPOLL   (Linux; scales to many descriptors)
//   -DVAPOR_IO_BACKEND_KQUEUE  (BSD, macOS; scales to many descriptors)
//   -DVAPOR_IO_BACKEND_POLL    (any POSIX; linear in the number of fds)
//   -DVAPOR_IO_BACKEND_SELECT  (oldest; descriptors must be < FD_SETSIZE)
// If none is defined, epoll is used on Linux, kqueue on BSD/macOS and poll
// elsewhere. Asking for a backend the platform lacks is a compile error.

#if defined(VAPOR_IO_BACKEND_EPOLL)
#define VAPOR_IO_DEFAULT_KIND 1
#elif defined(VAPOR_IO_BACKEND_KQUEUE)
#define VAPOR_IO_DEFAULT_KIND 4
#elif defined(VAPOR_IO_BACKEND_POLL)
#define VAPOR_IO_DEFAULT_KIND 2
#elif defined(VAPOR_IO_BACKEND_SELECT)
#define VAPOR_IO_DEFAULT_KIND 3
#elif VAPOR_IO_HAS_EPOLL
#define VAPOR_IO_DEFAULT_KIND 1
#elif VAPOR_IO_HAS_KQUEUE
#define VAPOR_IO_DEFAULT_KIND 4
#else
#define VAPOR_IO_DEFAULT_KIND 2
#endif

#if VAPOR_IO_DEFAULT_KIND == 1 && !VAPOR_IO_HAS_EPOLL
#error "VAPOR_IO_BACKEND_EPOLL requires Linux"
#endif
#if VAPOR_IO_DEFAULT_KIND == 4 && !VAPOR_IO_HAS_KQUEUE
#error "VAPOR_IO_BACKEND_KQUEUE requires a BSD or macOS"
#endif

namespace vapor {

namespace detail {

inline Expected<void, CommonError> PushEvent(Vector<PollEvent> &out, int fd,
                                             unsigned ev) noexcept {
  PollEvent e;
  e.fd = fd;
  e.events = ev;
  return out.push_back(e);
}

} // namespace detail

// poll(2): works everywhere, cost grows with the number of descriptors.
class PollPollBackend : public PollBackend {
  Vector<struct pollfd> m_fds;

  static short ToPoll(unsigned ev) noexcept {
    short s = 0;
    if (ev & IoEvent::Read)
      s = static_cast<short>(s | POLLIN);
    if (ev & IoEvent::Write)
      s = static_cast<short>(s | POLLOUT);
    return s;
  }
  size_t IndexOf(int fd) const noexcept {
    for (size_t i = 0; i < m_fds.size(); ++i)
      if (m_fds[i].fd == fd)
        return i;
    return m_fds.size();
  }

public:
  explicit PollPollBackend(Allocator &alloc) noexcept : m_fds(alloc) {}

  Expected<void, CommonError> Add(int fd, unsigned events) noexcept override {
    if (fd < 0)
      return MakeUnexpected(CommonError::BadDescriptor);
    if (IndexOf(fd) != m_fds.size())
      return MakeUnexpected(CommonError::AlreadyExists);
    struct pollfd p;
    p.fd = fd;
    p.events = ToPoll(events);
    p.revents = 0;
    return m_fds.push_back(p);
  }
  Expected<void, CommonError> Modify(int fd,
                                     unsigned events) noexcept override {
    size_t i = IndexOf(fd);
    if (i == m_fds.size())
      return MakeUnexpected(CommonError::ValueNotFound);
    m_fds[i].events = ToPoll(events);
    return {};
  }
  void Remove(int fd) noexcept override {
    size_t i = IndexOf(fd);
    if (i == m_fds.size())
      return;
    m_fds[i] = m_fds[m_fds.size() - 1];
    m_fds.pop_back();
  }
  Expected<void, CommonError> Wait(int timeout_ms,
                                   Vector<PollEvent> &out) noexcept override {
    out.clear();
    int n = ::poll(m_fds.data(), static_cast<nfds_t>(m_fds.size()), timeout_ms);
    if (n < 0) {
      if (errno == EINTR)
        return {};
      return MakeUnexpected(ErrorFromErrno(errno));
    }
    for (size_t i = 0; i < m_fds.size() && n > 0; ++i) {
      short r = m_fds[i].revents;
      if (r == 0)
        continue;
      --n;
      unsigned ev = 0;
      if (r & POLLIN)
        ev |= IoEvent::Read;
      if (r & POLLOUT)
        ev |= IoEvent::Write;
      if (r & (POLLERR | POLLNVAL))
        ev |= IoEvent::Error;
      if (r & POLLHUP)
        ev |= IoEvent::Hangup;
      Expected<void, CommonError> p = detail::PushEvent(out, m_fds[i].fd, ev);
      if (!p)
        return p;
    }
    return {};
  }
};

// select(2): the oldest interface; descriptors must be below FD_SETSIZE.
class SelectBackend : public PollBackend {
  struct Entry {
    int fd;
    unsigned events;
  };
  Vector<Entry> m_entries;

  size_t IndexOf(int fd) const noexcept {
    for (size_t i = 0; i < m_entries.size(); ++i)
      if (m_entries[i].fd == fd)
        return i;
    return m_entries.size();
  }

public:
  explicit SelectBackend(Allocator &alloc) noexcept : m_entries(alloc) {}

  Expected<void, CommonError> Add(int fd, unsigned events) noexcept override {
    if (fd < 0)
      return MakeUnexpected(CommonError::BadDescriptor);
    if (fd >= FD_SETSIZE)
      return MakeUnexpected(CommonError::InvalidArgument);
    if (IndexOf(fd) != m_entries.size())
      return MakeUnexpected(CommonError::AlreadyExists);
    Entry e;
    e.fd = fd;
    e.events = events;
    return m_entries.push_back(e);
  }
  Expected<void, CommonError> Modify(int fd,
                                     unsigned events) noexcept override {
    size_t i = IndexOf(fd);
    if (i == m_entries.size())
      return MakeUnexpected(CommonError::ValueNotFound);
    m_entries[i].events = events;
    return {};
  }
  void Remove(int fd) noexcept override {
    size_t i = IndexOf(fd);
    if (i == m_entries.size())
      return;
    m_entries[i] = m_entries[m_entries.size() - 1];
    m_entries.pop_back();
  }
  Expected<void, CommonError> Wait(int timeout_ms,
                                   Vector<PollEvent> &out) noexcept override {
    out.clear();
    fd_set rd, wr, ex;
    FD_ZERO(&rd);
    FD_ZERO(&wr);
    FD_ZERO(&ex);
    int maxfd = -1;
    for (size_t i = 0; i < m_entries.size(); ++i) {
      int fd = m_entries[i].fd;
      if (m_entries[i].events & IoEvent::Read)
        FD_SET(fd, &rd);
      if (m_entries[i].events & IoEvent::Write)
        FD_SET(fd, &wr);
      FD_SET(fd, &ex);
      if (fd > maxfd)
        maxfd = fd;
    }
    struct timeval tv;
    struct timeval *ptv = nullptr;
    if (timeout_ms >= 0) {
      tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
      tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
      ptv = &tv;
    }
    int n = ::select(maxfd + 1, &rd, &wr, &ex, ptv);
    if (n < 0) {
      if (errno == EINTR)
        return {};
      return MakeUnexpected(ErrorFromErrno(errno));
    }
    for (size_t i = 0; i < m_entries.size() && n > 0; ++i) {
      int fd = m_entries[i].fd;
      unsigned ev = 0;
      if (FD_ISSET(fd, &rd))
        ev |= IoEvent::Read;
      if (FD_ISSET(fd, &wr))
        ev |= IoEvent::Write;
      if (FD_ISSET(fd, &ex))
        ev |= IoEvent::Error;
      if (ev == 0)
        continue;
      --n;
      Expected<void, CommonError> p = detail::PushEvent(out, fd, ev);
      if (!p)
        return p;
    }
    return {};
  }
};

#if VAPOR_IO_HAS_EPOLL
// epoll(7): Linux; cost depends on the number of *ready* descriptors.
class EpollBackend : public PollBackend {
  Fd m_ep;

  static uint32_t ToEpoll(unsigned ev) noexcept {
    uint32_t e = 0;
    if (ev & IoEvent::Read)
      e |= EPOLLIN;
    if (ev & IoEvent::Write)
      e |= EPOLLOUT;
    return e;
  }
  Expected<void, CommonError> Ctl(int op, int fd, unsigned events) noexcept {
    struct epoll_event ee;
    ee.events = ToEpoll(events);
    ee.data.fd = fd;
    if (::epoll_ctl(m_ep.get(), op, fd, &ee) != 0)
      return MakeUnexpected(ErrorFromErrno(errno));
    return {};
  }

public:
  EpollBackend() noexcept : m_ep(::epoll_create1(EPOLL_CLOEXEC)) {}

  bool ok() const noexcept { return m_ep.valid(); }
  bool ConcurrentMutationSafe() const noexcept override { return true; }

  Expected<void, CommonError> Add(int fd, unsigned events) noexcept override {
    return Ctl(EPOLL_CTL_ADD, fd, events);
  }
  Expected<void, CommonError> Modify(int fd,
                                     unsigned events) noexcept override {
    return Ctl(EPOLL_CTL_MOD, fd, events);
  }
  void Remove(int fd) noexcept override {
    struct epoll_event ee;
    ee.events = 0;
    ee.data.fd = fd;
    (void)::epoll_ctl(m_ep.get(), EPOLL_CTL_DEL, fd, &ee);
  }
  Expected<void, CommonError> Wait(int timeout_ms,
                                   Vector<PollEvent> &out) noexcept override {
    out.clear();
    struct epoll_event evs[64];
    int n = ::epoll_wait(m_ep.get(), evs, 64, timeout_ms);
    if (n < 0) {
      if (errno == EINTR)
        return {};
      return MakeUnexpected(ErrorFromErrno(errno));
    }
    for (int i = 0; i < n; ++i) {
      uint32_t r = evs[i].events;
      unsigned ev = 0;
      if (r & EPOLLIN)
        ev |= IoEvent::Read;
      if (r & EPOLLOUT)
        ev |= IoEvent::Write;
      if (r & EPOLLERR)
        ev |= IoEvent::Error;
      if (r & (EPOLLHUP | EPOLLRDHUP))
        ev |= IoEvent::Hangup;
      Expected<void, CommonError> p =
          detail::PushEvent(out, evs[i].data.fd, ev);
      if (!p)
        return p;
    }
    return {};
  }
};
#endif

#if VAPOR_IO_HAS_KQUEUE
// kqueue(2): BSD and macOS.
class KqueueBackend : public PollBackend {
  Fd m_kq;
  Vector<PollEvent> m_interest; // fd + events currently registered

  size_t IndexOf(int fd) const noexcept {
    for (size_t i = 0; i < m_interest.size(); ++i)
      if (m_interest[i].fd == fd)
        return i;
    return m_interest.size();
  }
  Expected<void, CommonError> Apply(int fd, unsigned old_ev,
                                    unsigned new_ev) noexcept {
    struct kevent ch[2];
    int n = 0;
    unsigned masks[2] = {IoEvent::Read, IoEvent::Write};
    short filters[2] = {EVFILT_READ, EVFILT_WRITE};
    for (int i = 0; i < 2; ++i) {
      bool was = (old_ev & masks[i]) != 0;
      bool now = (new_ev & masks[i]) != 0;
      if (was == now)
        continue;
      EV_SET(&ch[n], static_cast<uintptr_t>(fd), filters[i],
             now ? EV_ADD : EV_DELETE, 0, 0, nullptr);
      ++n;
    }
    if (n > 0 && ::kevent(m_kq.get(), ch, n, nullptr, 0, nullptr) != 0)
      return MakeUnexpected(ErrorFromErrno(errno));
    return {};
  }

public:
  explicit KqueueBackend(Allocator &alloc) noexcept
      : m_kq(::kqueue()), m_interest(alloc) {}

  bool ok() const noexcept { return m_kq.valid(); }

  Expected<void, CommonError> Add(int fd, unsigned events) noexcept override {
    if (IndexOf(fd) != m_interest.size())
      return MakeUnexpected(CommonError::AlreadyExists);
    Expected<void, CommonError> r = Apply(fd, 0, events);
    if (!r)
      return r;
    PollEvent e;
    e.fd = fd;
    e.events = events;
    r = m_interest.push_back(e);
    if (!r)
      (void)Apply(fd, events, 0);
    return r;
  }
  Expected<void, CommonError> Modify(int fd,
                                     unsigned events) noexcept override {
    size_t i = IndexOf(fd);
    if (i == m_interest.size())
      return MakeUnexpected(CommonError::ValueNotFound);
    Expected<void, CommonError> r = Apply(fd, m_interest[i].events, events);
    if (r)
      m_interest[i].events = events;
    return r;
  }
  void Remove(int fd) noexcept override {
    size_t i = IndexOf(fd);
    if (i == m_interest.size())
      return;
    (void)Apply(fd, m_interest[i].events, 0);
    m_interest[i] = m_interest[m_interest.size() - 1];
    m_interest.pop_back();
  }
  Expected<void, CommonError> Wait(int timeout_ms,
                                   Vector<PollEvent> &out) noexcept override {
    out.clear();
    struct kevent evs[64];
    struct timespec ts;
    struct timespec *pts = nullptr;
    if (timeout_ms >= 0) {
      ts.tv_sec = static_cast<time_t>(timeout_ms / 1000);
      ts.tv_nsec = static_cast<long>(timeout_ms % 1000) * 1000000L;
      pts = &ts;
    }
    int n = ::kevent(m_kq.get(), nullptr, 0, evs, 64, pts);
    if (n < 0) {
      if (errno == EINTR)
        return {};
      return MakeUnexpected(ErrorFromErrno(errno));
    }
    // kqueue reports read and write as separate events; merge per descriptor.
    for (int i = 0; i < n; ++i) {
      int fd = static_cast<int>(evs[i].ident);
      unsigned ev = 0;
      if (evs[i].filter == EVFILT_READ)
        ev |= IoEvent::Read;
      if (evs[i].filter == EVFILT_WRITE)
        ev |= IoEvent::Write;
      if (evs[i].flags & EV_ERROR)
        ev |= IoEvent::Error;
      if (evs[i].flags & EV_EOF)
        ev |= IoEvent::Hangup;
      size_t j = 0;
      for (; j < out.size(); ++j)
        if (out[j].fd == fd)
          break;
      if (j < out.size()) {
        out[j].events |= ev;
        continue;
      }
      Expected<void, CommonError> p = detail::PushEvent(out, fd, ev);
      if (!p)
        return p;
    }
    return {};
  }
};
#endif

} // namespace vapor

#endif // VAPOR_HAS_POSIX_IO
