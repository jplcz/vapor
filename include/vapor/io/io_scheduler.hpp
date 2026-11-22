#pragma once

#include <vapor/allocator.hpp>
#include <vapor/chrono.hpp>
#include <vapor/deque.hpp>
#include <vapor/detail/mutex.hpp>
#include <vapor/detail/placement_new.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/functional.hpp>
#include <vapor/io/fd.hpp>
#include <vapor/io/io_backends.hpp>
#include <vapor/io/poll_backend.hpp>
#include <vapor/map.hpp>
#include <vapor/vector.hpp>

// IoScheduler: a small "event loop" (reactor), in the spirit of Boost.Asio
// but with far fewer features. Any number of threads may run it together.
//
// Why: programs that talk to sockets, pipes or timers need to wait for many
// things at once without a thread per thing. The loop sleeps in the operating
// system until something is ready, then calls the matching callback. Doing
// this by hand with poll/epoll/kqueue is repetitive and easy to get wrong
// (timeouts, interrupted calls, removing a watch while it is being reported,
// waking the loop from another thread...). IoScheduler does that once. When
// callbacks are slow or numerous, several threads can call Run() on the same
// scheduler and share the work, exactly like Asio's io_context::run().
//
// What it does:
//   - Post(task)            run `task` soon, on whichever thread is in Run
//   - RunAfter/RunAt/RunEvery/Cancel   one-shot and repeating timers
//   - Watch/Modify/Unwatch  call back when a file descriptor is readable,
//                           writable, errored or hung up (level-triggered:
//                           reported again while it stays ready, so the
//                           callback must read/write or Unwatch)
//   - Run / RunOnce / Stop
//
// How:
//
//   IoScheduler io;
//   if (!io.Open()) { /* e.g. UnsupportedOperation */ }
//   auto t = Task::Create([&] { puts("hello"); });
//   (void)io.RunAfter(Duration::FromMilliseconds(50), Move(t.value()));
//   (void)io.Post(Move(other_task));        // from any thread
//   auto cb = IoCallback::Create([&](IoReady r) {
//     if (r.events & IoEvent::Read) { /* read(r.fd, ...) */ }
//   });
//   auto w = io.Watch(fd, IoEvent::Read, Move(cb.value()));
//   (void)io.Run();                         // returns after Stop()
//
// Threads. Every public function may be called from any thread at any time.
// Several threads may be inside Run()/RunOnce() at once:
//   - a given watch, and a given timer, never has two callbacks running at the
//     same time (a repeating tick that is due while the previous tick still
//     runs is skipped); different ones may run in parallel;
//   - posted tasks start in FIFO order but may overlap and finish in any
//     order;
//   - Unwatch/Cancel return at once, even if that callback is running on
//     another thread right now and keeps running until it returns, so state a
//     callback uses must stay alive until then (SharedPtr helps);
//   - callbacks run with no lock held, so they may call any scheduler
//     function, including Unwatch/Cancel on themselves.
//   Internally one thread at a time (the "leader") sleeps in the operating
//   system; the others wait for ready work and take it. While a watch's
//   callback runs, the descriptor is taken out of the operating system's
//   wait set and put back afterwards (still level-triggered for the user). If
//   putting it back fails (for example the callback closed the descriptor
//   without Unwatch) the watch is dropped.
//   The allocator passed to the constructor must be thread-safe when more than
//   one thread uses the scheduler. Define VAPOR_IO_SINGLE_THREADED to remove
//   all locking; then every call (including Post and Stop) must come from the
//   one thread that runs the loop.
//
// Other rules:
//   - Ids (TimerId, WatchId) are never reused, so a stale id is "not found".
//   - Each RunOnce round does: wait, then due timers, then ready descriptors,
//     then posted tasks (in one thread: strictly in that order).
//   - Repeating timers keep their original rhythm (no drift) and skip ticks
//     missed while the loop was busy.
//   - Stop() makes every thread in Run() return after its current callback;
//     Stop() before Run() makes the next Run return at once. The flag resets
//     when the last Run returns.
//   - Callbacks must not call Run/RunOnce re-entrantly.
//   - The operating-system mechanism is chosen by IoBackendKind or the
//     VAPOR_IO_BACKEND_* defines (see io_backends.hpp). On platforms without
//     POSIX I/O, Open() returns UnsupportedOperation.

namespace vapor {

typedef Function<void()> Task;

struct IoReady {
  int fd;
  unsigned events; // IoEvent::Flag bits
};
typedef Function<void(IoReady)> IoCallback;

typedef uint64_t TimerId; // 0 is never a valid id
typedef uint64_t WatchId; // 0 is never a valid id

enum class IoBackendKind { Default, Epoll, Poll, Select, Kqueue };

class IoScheduler {
  struct TimerKey {
    int64_t ns;
    uint64_t id;
  };
  struct TimerKeyLess {
    bool operator()(const TimerKey &a, const TimerKey &b) const noexcept {
      return a.ns < b.ns || (a.ns == b.ns && a.id < b.id);
    }
  };
  struct Timer {
    int64_t deadline_ns;
    int64_t period_ns; // 0 = one-shot
    Task cb;
    bool busy;    // queued or running; periodic ticks are skipped meanwhile
    bool dropped; // could not be rescheduled: remove after the current run
    Timer(int64_t d, int64_t p, Task &&t) noexcept
        : deadline_ns(d), period_ns(p), cb(Move(t)), busy(false),
          dropped(false) {}
  };
  struct WatchEntry {
    int fd;
    unsigned interest;
    IoCallback cb;
    bool armed; // registered with the backend (false while its callback runs)
    WatchEntry(int f, unsigned i, IoCallback &&c) noexcept
        : fd(f), interest(i), cb(Move(c)), armed(true) {}
  };
  enum ItemKind { ItemTimer, ItemWatch };
  struct ReadyItem {
    int kind;
    uint64_t id;
    unsigned events;
  };

  Allocator &m_alloc;
  PollBackend *m_backend;
  void *m_backend_mem;
  size_t m_backend_size;
  Fd m_wake_r;
  Fd m_wake_w;

  // Everything below is guarded by m_mutex.
  detail::Mutex m_mutex;
  detail::ConditionVariable m_cond;
  Deque<Task> m_posted;
  Deque<ReadyItem> m_ready;
  Map<uint64_t, Timer> m_timers;
  Map<TimerKey, uint64_t, TimerKeyLess> m_order;
  Map<uint64_t, WatchEntry> m_watches;
  Map<int, uint64_t> m_by_fd;
  Vector<PollEvent> m_events; // only used by the leader
  uint64_t m_next_id;
  bool m_stop;
  size_t m_runners;   // threads inside Run()
  size_t m_followers; // threads waiting for work or leadership
  size_t m_mutators;  // threads that want to change a non-concurrent backend
  bool m_leader;      // a thread is polling or converting poll results
  bool m_waiting;     // the leader is inside the backend's Wait

  static int64_t NowNs() noexcept {
    return SteadyClock::Now().SinceOrigin().nanoseconds();
  }
  static int64_t ClampNs(int64_t ns) noexcept {
    const int64_t cap = IntLimits<int64_t>::Max() / 4;
    return ns > cap ? cap : ns;
  }

  template <typename B, typename... Args>
  Expected<void, CommonError> Construct(Args &&...args) noexcept {
    void *mem = m_alloc.Allocate(sizeof(B), alignof(B));
    if (!mem)
      return MakeUnexpected(CommonError::OutOfMemory);
    B *b = detail::ConstructAt<B>(mem, Forward<Args>(args)...);
    m_backend = b;
    m_backend_mem = mem;
    m_backend_size = sizeof(B);
    return {};
  }

  void DestroyBackend() noexcept {
    if (m_backend_mem) {
      m_backend->~PollBackend();
      m_alloc.Deallocate(m_backend_mem, m_backend_size);
    }
    m_backend = nullptr;
    m_backend_mem = nullptr;
  }

public:
  explicit IoScheduler(
      Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_alloc(alloc), m_backend(nullptr), m_backend_mem(nullptr),
        m_backend_size(0), m_posted(alloc), m_ready(alloc), m_timers(alloc),
        m_order(alloc), m_watches(alloc), m_by_fd(alloc), m_events(alloc),
        m_next_id(1), m_stop(false), m_runners(0), m_followers(0),
        m_mutators(0), m_leader(false), m_waiting(false) {}

  IoScheduler(const IoScheduler &) = delete;
  IoScheduler &operator=(const IoScheduler &) = delete;

  // No thread may be inside Run/RunOnce or using the scheduler any more.
  ~IoScheduler() {
    if (m_backend && m_wake_r.valid())
      m_backend->Remove(m_wake_r.get());
    DestroyBackend();
  }

  // Creates the wake-up pipe and the operating-system backend. Call once,
  // before the scheduler is shared with other threads.
  Expected<void, CommonError>
  Open(IoBackendKind kind = IoBackendKind::Default) noexcept {
#if VAPOR_HAS_POSIX_IO
    if (m_backend)
      return MakeUnexpected(CommonError::AlreadyExists);
    if (kind == IoBackendKind::Default) {
#if VAPOR_IO_DEFAULT_KIND == 1
      kind = IoBackendKind::Epoll;
#elif VAPOR_IO_DEFAULT_KIND == 2
      kind = IoBackendKind::Poll;
#elif VAPOR_IO_DEFAULT_KIND == 3
      kind = IoBackendKind::Select;
#else
      kind = IoBackendKind::Kqueue;
#endif
    }
    Expected<void, CommonError> r = {};
    switch (kind) {
    case IoBackendKind::Poll:
      r = Construct<PollPollBackend>(m_alloc);
      break;
    case IoBackendKind::Select:
      r = Construct<SelectBackend>(m_alloc);
      break;
    case IoBackendKind::Epoll:
#if VAPOR_IO_HAS_EPOLL
      r = Construct<EpollBackend>();
      if (r && !static_cast<EpollBackend *>(m_backend)->ok()) {
        int e = errno;
        DestroyBackend();
        return MakeUnexpected(ErrorFromErrno(e));
      }
#else
      r = MakeUnexpected(CommonError::UnsupportedOperation);
#endif
      break;
    case IoBackendKind::Kqueue:
#if VAPOR_IO_HAS_KQUEUE
      r = Construct<KqueueBackend>(m_alloc);
      if (r && !static_cast<KqueueBackend *>(m_backend)->ok()) {
        int e = errno;
        DestroyBackend();
        return MakeUnexpected(ErrorFromErrno(e));
      }
#else
      r = MakeUnexpected(CommonError::UnsupportedOperation);
#endif
      break;
    default:
      r = MakeUnexpected(CommonError::InvalidArgument);
      break;
    }
    if (!r)
      return r;
    Expected<Pair<Fd, Fd>, CommonError> p = Fd::MakePipe();
    if (!p) {
      DestroyBackend();
      return MakeUnexpected(p.error());
    }
    m_wake_r = Move(p.value().first);
    m_wake_w = Move(p.value().second);
    r = m_backend->Add(m_wake_r.get(), IoEvent::Read);
    if (!r) {
      DestroyBackend();
      m_wake_r.reset();
      m_wake_w.reset();
    }
    return r;
#else
    (void)kind;
    return MakeUnexpected(CommonError::UnsupportedOperation);
#endif
  }

  VAPOR_NODISCARD bool is_open() const noexcept { return m_backend != nullptr; }

  // Queues `task` to run on a thread that is inside Run/RunOnce.
  VAPOR_NODISCARD Expected<void, CommonError> Post(Task task) noexcept {
    if (!m_backend)
      return MakeUnexpected(CommonError::BadDescriptor);
    detail::MutexLock lock(m_mutex);
    Expected<void, CommonError> r = m_posted.push_back(Move(task));
    if (r)
      NotifyLocked();
    return r;
  }

  // Makes every thread in Run() return after its current callback.
  void Stop() noexcept {
    detail::MutexLock lock(m_mutex);
    m_stop = true;
    m_cond.notify_all();
    if (m_waiting)
      Wake();
  }

  // ---- timers

  VAPOR_NODISCARD Expected<TimerId, CommonError> RunAfter(Duration delay,
                                                          Task task) noexcept {
    int64_t d = delay.nanoseconds();
    return AddTimer(NowNs() + ClampNs(d < 0 ? 0 : d), 0, Move(task));
  }
  VAPOR_NODISCARD Expected<TimerId, CommonError> RunAt(TimePoint when,
                                                       Task task) noexcept {
    return AddTimer(when.SinceOrigin().nanoseconds(), 0, Move(task));
  }
  // First run after `period`, then every `period`.
  VAPOR_NODISCARD Expected<TimerId, CommonError> RunEvery(Duration period,
                                                          Task task) noexcept {
    if (period.nanoseconds() <= 0)
      return MakeUnexpected(CommonError::InvalidArgument);
    int64_t p = ClampNs(period.nanoseconds());
    return AddTimer(NowNs() + p, p, Move(task));
  }
  // True when a pending (or currently running) timer was removed.
  bool Cancel(TimerId id) noexcept {
    detail::MutexLock lock(m_mutex);
    Map<uint64_t, Timer>::iterator it = m_timers.find(id);
    if (it == m_timers.end())
      return false;
    TimerKey k;
    k.ns = it->second.deadline_ns;
    k.id = id;
    m_order.erase(k);
    m_timers.erase(id);
    return true;
  }

  // ---- descriptor watches

  VAPOR_NODISCARD Expected<WatchId, CommonError>
  Watch(int fd, unsigned interest, IoCallback cb) noexcept {
    if (!m_backend)
      return MakeUnexpected(CommonError::BadDescriptor);
    if (fd < 0)
      return MakeUnexpected(CommonError::BadDescriptor);
    interest &= (IoEvent::Read | IoEvent::Write);
    if (interest == 0)
      return MakeUnexpected(CommonError::InvalidArgument);
    detail::MutexLock lock(m_mutex);
    if (fd == m_wake_r.get() || m_by_fd.find(fd) != m_by_fd.end())
      return MakeUnexpected(CommonError::AlreadyExists);
    uint64_t id = m_next_id;
    Expected<Pair<Map<uint64_t, WatchEntry>::iterator, bool>, CommonError> a =
        m_watches.try_emplace(id, fd, interest, Move(cb));
    if (!a)
      return MakeUnexpected(a.error());
    Expected<Pair<Map<int, uint64_t>::iterator, bool>, CommonError> b =
        m_by_fd.insert(Pair<const int, uint64_t>(fd, id));
    if (!b) {
      m_watches.erase(id);
      return MakeUnexpected(b.error());
    }
    AcquireBackendLocked();
    Expected<void, CommonError> r = m_backend->Add(fd, interest);
    ReleaseBackendLocked();
    if (!r) {
      m_by_fd.erase(fd);
      m_watches.erase(id);
      return MakeUnexpected(r.error());
    }
    ++m_next_id;
    return id;
  }

  VAPOR_NODISCARD Expected<void, CommonError>
  Modify(WatchId id, unsigned interest) noexcept {
    detail::MutexLock lock(m_mutex);
    Map<uint64_t, WatchEntry>::iterator it = m_watches.find(id);
    if (it == m_watches.end())
      return MakeUnexpected(CommonError::ValueNotFound);
    interest &= (IoEvent::Read | IoEvent::Write);
    if (interest == 0)
      return MakeUnexpected(CommonError::InvalidArgument);
    if (!it->second.armed) { // callback running: applied when it is re-armed
      it->second.interest = interest;
      return {};
    }
    AcquireBackendLocked();
    Expected<void, CommonError> r = m_backend->Modify(it->second.fd, interest);
    ReleaseBackendLocked();
    if (r)
      it->second.interest = interest;
    return r;
  }

  // True when a watch was removed. Does not close the descriptor.
  bool Unwatch(WatchId id) noexcept {
    detail::MutexLock lock(m_mutex);
    Map<uint64_t, WatchEntry>::iterator it = m_watches.find(id);
    if (it == m_watches.end())
      return false;
    int fd = it->second.fd;
    AcquireBackendLocked();
    m_backend->Remove(fd);
    ReleaseBackendLocked();
    m_by_fd.erase(fd);
    m_watches.erase(id);
    return true;
  }

  // ---- running

  // One round: wait up to `timeout` (or until the next timer / something to
  // do), then run what is ready. Returns an error only when the operating
  // system's wait call fails.
  Expected<void, CommonError>
  RunOnce(Duration timeout = Duration::Max()) noexcept {
    if (!m_backend)
      return MakeUnexpected(CommonError::BadDescriptor);
    bool bounded = timeout != Duration::Max();
    int64_t t = timeout.nanoseconds();
    int64_t deadline = bounded ? NowNs() + ClampNs(t < 0 ? 0 : t) : 0;
    detail::MutexLock lock(m_mutex);
    return Round(false, bounded, deadline);
  }

  // Runs rounds until Stop(). May be called by several threads at once.
  Expected<void, CommonError> Run() noexcept {
    if (!m_backend)
      return MakeUnexpected(CommonError::BadDescriptor);
    detail::MutexLock lock(m_mutex);
    ++m_runners;
    Expected<void, CommonError> r = {};
    while (!m_stop) {
      r = Round(true, false, 0);
      if (!r)
        break;
    }
    if (--m_runners == 0)
      m_stop = false;
    return r;
  }

private:
  void Wake() noexcept {
#if VAPOR_HAS_POSIX_IO
    if (!m_wake_w.valid())
      return;
    char b = 1;
    ssize_t n;
    do {
      n = ::write(m_wake_w.get(), &b, 1);
    } while (n < 0 && errno == EINTR);
    // EAGAIN means the pipe is already full of wake-ups: the loop will wake.
#endif
  }

  void DrainWake() noexcept {
#if VAPOR_HAS_POSIX_IO
    char buf[64];
    for (;;) {
      ssize_t n = ::read(m_wake_r.get(), buf, sizeof(buf));
      if (n > 0)
        continue;
      if (n < 0 && errno == EINTR)
        continue;
      break;
    }
#endif
  }

  // Called with m_mutex held after new work or a new timer appeared.
  void NotifyLocked() noexcept {
    if (m_followers > 0)
      m_cond.notify_all();
    else if (m_waiting)
      Wake();
  }

  // Backends such as poll and select read their tables while waiting, so
  // they may only be changed while the leader is out of Wait.
  void AcquireBackendLocked() noexcept {
    if (m_backend->ConcurrentMutationSafe())
      return;
    ++m_mutators;
    while (m_waiting) {
      Wake();
      m_cond.wait(m_mutex);
    }
  }
  void ReleaseBackendLocked() noexcept {
    if (m_backend->ConcurrentMutationSafe())
      return;
    if (--m_mutators == 0)
      m_cond.notify_all();
  }

  Expected<TimerId, CommonError> AddTimer(int64_t deadline, int64_t period,
                                          Task &&task) noexcept {
    if (!m_backend)
      return MakeUnexpected(CommonError::BadDescriptor);
    detail::MutexLock lock(m_mutex);
    uint64_t id = m_next_id;
    Expected<Pair<Map<uint64_t, Timer>::iterator, bool>, CommonError> a =
        m_timers.try_emplace(id, deadline, period, Move(task));
    if (!a)
      return MakeUnexpected(a.error());
    TimerKey k;
    k.ns = deadline;
    k.id = id;
    Expected<Pair<Map<TimerKey, uint64_t, TimerKeyLess>::iterator, bool>,
             CommonError>
        b = m_order.insert(Pair<const TimerKey, uint64_t>(k, id));
    if (!b) {
      m_timers.erase(id);
      return MakeUnexpected(b.error());
    }
    ++m_next_id;
    if (m_waiting)
      Wake(); // the leader may be sleeping past this deadline
    return id;
  }

  bool HasWorkLocked() const noexcept {
    return !m_ready.empty() || !m_posted.empty();
  }

  // m_mutex is held on entry and on return (released while sleeping and while
  // callbacks run).
  Expected<void, CommonError> Round(bool is_run, bool bounded,
                                    int64_t deadline) noexcept {
    for (;;) {
      if (is_run && m_stop)
        return {};
      if (HasWorkLocked())
        break;
      if (!m_leader) {
        Expected<void, CommonError> r = LeadLocked(bounded, deadline);
        if (!r)
          return r;
        break;
      }
      ++m_followers;
      if (bounded) {
        int64_t rem = deadline - NowNs();
        if (rem <= 0) {
          --m_followers;
          return {};
        }
        m_cond.wait_for_ns(m_mutex, rem);
      } else {
        m_cond.wait(m_mutex);
      }
      --m_followers;
    }
    DrainLocked();
    return {};
  }

  int ComputeWaitMsLocked(bool bounded, int64_t deadline) const noexcept {
    if (m_stop || HasWorkLocked())
      return 0;
    int64_t now = NowNs();
    int64_t ns = -1;
    if (bounded)
      ns = deadline > now ? deadline - now : 0;
    if (!m_order.empty()) {
      int64_t until = m_order.begin()->first.ns - now;
      if (until < 0)
        until = 0;
      if (ns < 0 || until < ns)
        ns = until;
    }
    if (ns < 0)
      return -1;
    int64_t ms = (ns + 999999) / 1000000;
    const int64_t max_ms = 2000000000;
    return static_cast<int>(ms > max_ms ? max_ms : ms);
  }

  Expected<void, CommonError> LeadLocked(bool bounded,
                                         int64_t deadline) noexcept {
    m_leader = true;
    while (m_mutators > 0)
      m_cond.wait(m_mutex);
    int ms = ComputeWaitMsLocked(bounded, deadline);
    m_waiting = true;
    m_mutex.unlock();
    Expected<void, CommonError> r = m_backend->Wait(ms, m_events);
    m_mutex.lock();
    m_waiting = false;
    if (r)
      ConvertLocked();
    m_leader = false;
    m_cond.notify_all(); // mutators may proceed; followers take work or lead
    return r;
  }

  // Turns the backend's results into ready items: due timers first, then
  // descriptors (posted tasks are taken straight from m_posted afterwards).
  void ConvertLocked() noexcept {
    for (size_t i = 0; i < m_events.size(); ++i)
      if (m_events[i].fd == m_wake_r.get())
        DrainWake();

    int64_t now = NowNs();
    while (!m_order.empty()) {
      Map<TimerKey, uint64_t, TimerKeyLess>::iterator oit = m_order.begin();
      if (oit->first.ns > now)
        break;
      TimerKey key = oit->first;
      uint64_t id = oit->second;
      Map<uint64_t, Timer>::iterator tit = m_timers.find(id);
      if (tit == m_timers.end()) {
        m_order.erase(key);
        continue;
      }
      Timer &t = tit->second;
      bool queue = !t.busy;
      if (queue) {
        ReadyItem item;
        item.kind = ItemTimer;
        item.id = id;
        item.events = 0;
        if (!m_ready.push_back(item))
          break; // out of memory: stays in m_order, retried next round
        t.busy = true;
      }
      m_order.erase(key);
      if (t.period_ns == 0)
        continue;
      int64_t next = key.ns + t.period_ns;
      if (next <= now)
        next += t.period_ns * ((now - next) / t.period_ns + 1);
      t.deadline_ns = next;
      TimerKey nk;
      nk.ns = next;
      nk.id = id;
      Expected<Pair<Map<TimerKey, uint64_t, TimerKeyLess>::iterator, bool>,
               CommonError>
          b = m_order.insert(Pair<const TimerKey, uint64_t>(nk, id));
      if (!b)
        t.dropped = true;
    }

    for (size_t i = 0; i < m_events.size(); ++i) {
      PollEvent ev = m_events[i];
      if (ev.fd == m_wake_r.get())
        continue;
      Map<int, uint64_t>::iterator fit = m_by_fd.find(ev.fd);
      if (fit == m_by_fd.end())
        continue;
      uint64_t id = fit->second;
      Map<uint64_t, WatchEntry>::iterator wit = m_watches.find(id);
      if (wit == m_watches.end() || !wit->second.armed)
        continue;
      unsigned got =
          ev.events & (wit->second.interest | IoEvent::Error | IoEvent::Hangup);
      if (got == 0)
        continue;
      ReadyItem item;
      item.kind = ItemWatch;
      item.id = id;
      item.events = got;
      if (!m_ready.push_back(item))
        continue; // still ready: reported again next round
      m_backend->Remove(ev.fd);
      wit->second.armed = false;
    }
  }

  // Runs at most as many items as were ready when it started, so callbacks
  // that keep posting cannot starve the next wait.
  void DrainLocked() noexcept {
    size_t budget = m_ready.size() + m_posted.size();
    for (; budget > 0; --budget) {
      if (!m_ready.empty()) {
        ReadyItem item = m_ready.front();
        m_ready.pop_front();
        if (item.kind == ItemTimer)
          RunTimerLocked(item.id);
        else
          RunWatchLocked(item.id, item.events);
      } else if (!m_posted.empty()) {
        Task t(Move(m_posted.front()));
        m_posted.pop_front();
        m_mutex.unlock();
        t();
        m_mutex.lock();
      } else {
        break;
      }
    }
  }

  void RunTimerLocked(uint64_t id) noexcept {
    Map<uint64_t, Timer>::iterator it = m_timers.find(id);
    if (it == m_timers.end())
      return;
    Task cb(Move(it->second.cb));
    m_mutex.unlock();
    cb();
    m_mutex.lock();
    it = m_timers.find(id);
    if (it == m_timers.end())
      return;
    if (it->second.period_ns == 0 || it->second.dropped) {
      m_timers.erase(id);
      return;
    }
    it->second.cb = Move(cb);
    it->second.busy = false;
  }

  void RunWatchLocked(uint64_t id, unsigned events) noexcept {
    Map<uint64_t, WatchEntry>::iterator it = m_watches.find(id);
    if (it == m_watches.end())
      return;
    IoCallback cb(Move(it->second.cb));
    IoReady ready;
    ready.fd = it->second.fd;
    ready.events = events;
    m_mutex.unlock();
    cb(Move(ready));
    m_mutex.lock();
    it = m_watches.find(id);
    if (it == m_watches.end())
      return;
    it->second.cb = Move(cb);
    int fd = it->second.fd;
    AcquireBackendLocked();
    Expected<void, CommonError> r = m_backend->Add(fd, it->second.interest);
    ReleaseBackendLocked();
    if (r) {
      it->second.armed = true;
    } else { // e.g. the callback closed the descriptor: drop the watch
      m_by_fd.erase(fd);
      m_watches.erase(id);
    }
  }
};

} // namespace vapor
