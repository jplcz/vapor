#include <gtest/gtest.h>

#include "test_allocator.hpp"

#include <vapor/atomic.hpp>
#include <vapor/io/fd.hpp>
#include <vapor/io/io_scheduler.hpp>

#include <pthread.h>
#include <sys/socket.h>

using namespace vapor;

namespace {

IoBackendKind AllKinds[] = {IoBackendKind::Default, IoBackendKind::Poll,
                            IoBackendKind::Select, IoBackendKind::Epoll,
                            IoBackendKind::Kqueue};

// Runs `body` for every backend this platform supports.
template <typename F> void ForEachBackend(F body) {
  for (size_t i = 0; i < sizeof(AllKinds) / sizeof(AllKinds[0]); ++i) {
    IoScheduler io;
    Expected<void, CommonError> r = io.Open(AllKinds[i]);
    if (!r) {
      ASSERT_EQ(r.error(), CommonError::UnsupportedOperation);
      continue;
    }
    body(io);
  }
}

template <typename F> Task MakeTask(F f) {
  Expected<Task, CommonError> t = Task::Create(f);
  EXPECT_TRUE(t.has_value());
  return Move(t.value());
}

template <typename F> IoCallback MakeCb(F f) {
  Expected<IoCallback, CommonError> t = IoCallback::Create(f);
  EXPECT_TRUE(t.has_value());
  return Move(t.value());
}

} // namespace

TEST(CommonErrorErrno, ValuesMatchErrno) {
  EXPECT_EQ(static_cast<int>(CommonError::OutOfMemory), ENOMEM);
  EXPECT_EQ(static_cast<int>(CommonError::InvalidArgument), EINVAL);
  EXPECT_EQ(static_cast<int>(CommonError::WouldBlock), EAGAIN);
  EXPECT_EQ(ErrorFromErrno(EPIPE), CommonError::BrokenPipe);
  EXPECT_EQ(ErrnoFromError(CommonError::Interrupted), EINTR);
}

TEST(IoFd, PipeAndFlags) {
  Expected<Pair<Fd, Fd>, CommonError> p = Fd::MakePipe();
  ASSERT_TRUE(p.has_value());
  Fd &r = p.value().first;
  Fd &w = p.value().second;
  EXPECT_TRUE(r.valid());
  char c;
  EXPECT_LT(::read(r.get(), &c, 1), 0);
  EXPECT_EQ(errno, EAGAIN);
  ASSERT_EQ(::write(w.get(), "x", 1), 1);
  ASSERT_EQ(::read(r.get(), &c, 1), 1);
  EXPECT_EQ(c, 'x');
  EXPECT_TRUE(r.SetNonBlocking(false).has_value());
  EXPECT_TRUE(r.SetNonBlocking(false).has_value());
}

TEST(IoFd, MoveReleaseDuplicate) {
  Expected<Pair<Fd, Fd>, CommonError> p = Fd::MakePipe();
  ASSERT_TRUE(p.has_value());
  Fd a(Move(p.value().first));
  EXPECT_FALSE(p.value().first.valid());
  Expected<Fd, CommonError> d = a.Duplicate();
  ASSERT_TRUE(d.has_value());
  EXPECT_NE(d.value().get(), a.get());
  int raw = d.value().release();
  EXPECT_FALSE(d.value().valid());
  ::close(raw);
  Fd none;
  EXPECT_EQ(none.Duplicate().error(), CommonError::BadDescriptor);
  EXPECT_EQ(none.SetNonBlocking(true).error(), CommonError::BadDescriptor);
}

TEST(IoScheduler, PostRunsInOrderAndStop) {
  ForEachBackend([](IoScheduler &io) {
    int log[3] = {0, 0, 0};
    int n = 0;
    ASSERT_TRUE(io.Post(MakeTask([&] { log[n++] = 1; })).has_value());
    ASSERT_TRUE(io.Post(MakeTask([&] { log[n++] = 2; })).has_value());
    ASSERT_TRUE(io.Post(MakeTask([&] {
                    log[n++] = 3;
                    io.Stop();
                  }))
                    .has_value());
    ASSERT_TRUE(io.Run().has_value());
    EXPECT_EQ(n, 3);
    EXPECT_EQ(log[0], 1);
    EXPECT_EQ(log[2], 3);
  });
}

TEST(IoScheduler, StopBeforeRunReturnsOnceThenResets) {
  IoScheduler io;
  ASSERT_TRUE(io.Open().has_value());
  io.Stop();
  ASSERT_TRUE(io.Run().has_value());
  bool ran = false;
  ASSERT_TRUE(io.Post(MakeTask([&] {
                  ran = true;
                  io.Stop();
                }))
                  .has_value());
  ASSERT_TRUE(io.Run().has_value());
  EXPECT_TRUE(ran);
}

namespace {
struct PostArg {
  IoScheduler *io;
  int *counter;
};
void *PostThread(void *p) {
  PostArg *a = static_cast<PostArg *>(p);
  struct timespec ts = {0, 20 * 1000 * 1000};
  nanosleep(&ts, nullptr);
  for (int i = 0; i < 100; ++i) {
    int *c = a->counter;
    (void)a->io->Post(MakeTask([c] { ++*c; }));
  }
  a->io->Stop();
  return nullptr;
}
} // namespace

TEST(IoScheduler, CrossThreadPostWakesBlockedLoop) {
  ForEachBackend([](IoScheduler &io) {
    int counter = 0;
    PostArg arg = {&io, &counter};
    pthread_t th;
    ASSERT_EQ(pthread_create(&th, nullptr, PostThread, &arg), 0);
    ASSERT_TRUE(io.Run().has_value()); // blocks with nothing to do
    pthread_join(th, nullptr);
    // Stop may arrive before the last tasks ran; drain the rest.
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(0)).has_value());
    EXPECT_EQ(counter, 100);
  });
}

TEST(IoScheduler, TimersOrderCancelAndPeriodic) {
  ForEachBackend([](IoScheduler &io) {
    int order[4] = {0, 0, 0, 0};
    int n = 0;
    auto a = io.RunAfter(Duration::FromMilliseconds(30),
                         MakeTask([&] { order[n++] = 3; }));
    auto b = io.RunAfter(Duration::FromMilliseconds(10),
                         MakeTask([&] { order[n++] = 1; }));
    auto c = io.RunAfter(Duration::FromMilliseconds(20),
                         MakeTask([&] { order[n++] = 2; }));
    auto dead = io.RunAfter(Duration::FromMilliseconds(15),
                            MakeTask([&] { order[n++] = 99; }));
    ASSERT_TRUE(a && b && c && dead);
    EXPECT_TRUE(io.Cancel(dead.value()));
    EXPECT_FALSE(io.Cancel(dead.value()));
    ASSERT_TRUE(io.RunAfter(Duration::FromMilliseconds(60),
                            MakeTask([&] { io.Stop(); }))
                    .has_value());
    ASSERT_TRUE(io.Run().has_value());
    EXPECT_EQ(n, 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);

    int ticks = 0;
    TimerId id = 0;
    auto e = io.RunEvery(Duration::FromMilliseconds(10), MakeTask([&] {
                           if (++ticks == 3) {
                             EXPECT_TRUE(io.Cancel(id));
                             io.Stop();
                           }
                         }));
    ASSERT_TRUE(e.has_value());
    id = e.value();
    ASSERT_TRUE(io.Run().has_value());
    EXPECT_EQ(ticks, 3);
    EXPECT_EQ(
        io.RunEvery(Duration::FromMilliseconds(0), MakeTask([] {})).error(),
        CommonError::InvalidArgument);
  });
}

TEST(IoScheduler, PeriodicSkipsMissedTicks) {
  IoScheduler io;
  ASSERT_TRUE(io.Open().has_value());
  int ticks = 0;
  ASSERT_TRUE(
      io.RunEvery(Duration::FromMilliseconds(5), MakeTask([&] { ++ticks; }))
          .has_value());
  SleepFor(Duration::FromMilliseconds(40));
  ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(0)).has_value());
  EXPECT_EQ(ticks, 1); // one catch-up run, not eight
}

TEST(IoScheduler, WatchReadWriteHangup) {
  ForEachBackend([](IoScheduler &io) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    Fd a(sv[0]), b(sv[1]);
    int reads = 0;
    unsigned last = 0;
    Expected<WatchId, CommonError> w =
        io.Watch(a.get(), IoEvent::Read, MakeCb([&](IoReady r) {
                   ++reads;
                   last = r.events;
                   char buf[8];
                   if (r.events & IoEvent::Read) {
                     ssize_t n = ::read(r.fd, buf, sizeof(buf));
                     (void)n;
                   }
                   if (reads == 2)
                     io.Stop();
                 }));
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(io.Watch(a.get(), IoEvent::Read, MakeCb([](IoReady) {})).error(),
              CommonError::AlreadyExists);
    ASSERT_EQ(::write(b.get(), "hi", 2), 2);
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(500)).has_value());
    EXPECT_EQ(reads, 1);
    EXPECT_TRUE(last & IoEvent::Read);
    b.reset(); // peer closes: readable (EOF) and/or hangup
    ASSERT_TRUE(io.Run().has_value());
    EXPECT_EQ(reads, 2);
    EXPECT_TRUE(io.Unwatch(w.value()));
    EXPECT_FALSE(io.Unwatch(w.value()));
  });
}

TEST(IoScheduler, LevelTriggeredRepeatsAndModifyWrite) {
  ForEachBackend([](IoScheduler &io) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    Fd a(sv[0]), b(sv[1]);
    ASSERT_EQ(::write(b.get(), "x", 1), 1);
    int hits = 0;
    Expected<WatchId, CommonError> w =
        io.Watch(a.get(), IoEvent::Read,
                 MakeCb([&](IoReady) { ++hits; })); // never reads
    ASSERT_TRUE(w.has_value());
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(100)).has_value());
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(100)).has_value());
    EXPECT_EQ(hits, 2);

    unsigned seen = 0;
    ASSERT_TRUE(io.Unwatch(w.value()));
    w = io.Watch(a.get(), IoEvent::Write,
                 MakeCb([&](IoReady r) { seen |= r.events; }));
    ASSERT_TRUE(w.has_value());
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(100)).has_value());
    EXPECT_TRUE(seen & IoEvent::Write);
    EXPECT_FALSE(seen & IoEvent::Read);
    ASSERT_TRUE(
        io.Modify(w.value(), IoEvent::Read | IoEvent::Write).has_value());
    seen = 0;
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(100)).has_value());
    EXPECT_TRUE(seen & IoEvent::Read);
    EXPECT_EQ(io.Modify(w.value(), 0).error(), CommonError::InvalidArgument);
    EXPECT_EQ(io.Modify(12345, IoEvent::Read).error(),
              CommonError::ValueNotFound);
  });
}

TEST(IoScheduler, UnwatchAndCancelInsideCallbacks) {
  ForEachBackend([](IoScheduler &io) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    Fd a(sv[0]), b(sv[1]);
    ASSERT_EQ(::write(b.get(), "x", 1), 1);
    int hits = 0;
    WatchId wid = 0;
    Expected<WatchId, CommonError> w = io.Watch(
        a.get(), IoEvent::Read, MakeCb([&](IoReady) {
          ++hits;
          EXPECT_TRUE(io.Unwatch(wid)); // removes the running callback itself
        }));
    ASSERT_TRUE(w.has_value());
    wid = w.value();
    TimerId victim = 0;
    auto first = io.RunAfter(Duration::FromMilliseconds(0),
                             MakeTask([&] { EXPECT_TRUE(io.Cancel(victim)); }));
    auto second = io.RunAfter(Duration::FromMilliseconds(0),
                              MakeTask([&] { hits += 100; }));
    ASSERT_TRUE(first && second);
    victim = second.value();
    SleepFor(Duration::FromMilliseconds(2));
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(100)).has_value());
    ASSERT_TRUE(io.RunOnce(Duration::FromMilliseconds(0)).has_value());
    EXPECT_EQ(hits, 1);
  });
}

TEST(IoScheduler, CallbacksCanScheduleMoreWork) {
  IoScheduler io;
  ASSERT_TRUE(io.Open().has_value());
  int depth = 0;
  struct Rec {
    static void Go(IoScheduler &io, int &depth) {
      if (++depth == 5) {
        io.Stop();
        return;
      }
      int *d = &depth;
      IoScheduler *p = &io;
      (void)io.Post(MakeTask([p, d] { Go(*p, *d); }));
    }
  };
  Rec::Go(io, depth);
  ASSERT_TRUE(io.Run().has_value());
  EXPECT_EQ(depth, 5);
}

TEST(IoScheduler, OutOfMemoryIsReported) {
  vapor_test::TestAllocator alloc;
  IoScheduler io(alloc);
  ASSERT_TRUE(io.Open().has_value());
  alloc.fail = true;
  EXPECT_FALSE(
      io.RunAfter(Duration::FromMilliseconds(1), MakeTask([] {})).has_value());
  EXPECT_FALSE(io.Post(MakeTask([] {})).has_value());
  alloc.fail = false;
  EXPECT_TRUE(
      io.RunAfter(Duration::FromMilliseconds(1), MakeTask([] {})).has_value());
}

TEST(IoScheduler, NoLeaksAfterDestroy) {
  vapor_test::TestAllocator alloc;
  {
    IoScheduler io(alloc);
    ASSERT_TRUE(io.Open().has_value());
    (void)io.RunAfter(Duration::FromSeconds(10), MakeTask([] {}));
    (void)io.Post(MakeTask([] {}));
  }
  EXPECT_EQ(alloc.live, 0);
}

TEST(IoScheduler, UseBeforeOpenFails) {
  IoScheduler io;
  EXPECT_FALSE(io.is_open());
  EXPECT_FALSE(io.Post(MakeTask([] {})).has_value());
  EXPECT_FALSE(io.RunOnce().has_value());
}

// ---- several threads inside Run()

namespace {

void *RunThread(void *p) {
  (void)static_cast<IoScheduler *>(p)->Run();
  return nullptr;
}

struct Pool {
  pthread_t th[4];
  size_t n;
  explicit Pool(IoScheduler &io, size_t count) : n(count) {
    for (size_t i = 0; i < n; ++i)
      pthread_create(&th[i], nullptr, RunThread, &io);
  }
  void Join() {
    for (size_t i = 0; i < n; ++i)
      pthread_join(th[i], nullptr);
    n = 0;
  }
};

} // namespace

TEST(IoSchedulerThreads, PostedTasksRunExactlyOnce) {
  ForEachBackend([](IoScheduler &io) {
    Atomic<int> done(0);
    Pool pool(io, 4);
    for (int i = 0; i < 2000; ++i) {
      ASSERT_TRUE(io.Post(MakeTask([&] {
                      if (done.fetch_add(1) == 1999)
                        io.Stop();
                    }))
                      .has_value());
    }
    pool.Join();
    EXPECT_EQ(done.load(), 2000);
  });
}

TEST(IoSchedulerThreads, StopReleasesEveryIdleThread) {
  ForEachBackend([](IoScheduler &io) {
    Pool pool(io, 4);
    SleepFor(Duration::FromMilliseconds(20));
    io.Stop();
    pool.Join();
  });
}

TEST(IoSchedulerThreads, WatchNeverOverlapsItself) {
  ForEachBackend([](IoScheduler &io) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    Fd a(sv[0]), b(sv[1]);
    ASSERT_TRUE(a.SetNonBlocking(true).has_value());
    Atomic<int> inside(0), overlaps(0), got(0);
    Expected<WatchId, CommonError> w =
        io.Watch(a.get(), IoEvent::Read, MakeCb([&](IoReady r) {
                   if (inside.fetch_add(1) != 0)
                     overlaps.fetch_add(1);
                   char c;
                   if (::read(r.fd, &c, 1) == 1 && got.fetch_add(1) == 29)
                     io.Stop();
                   SleepFor(Duration::FromMilliseconds(1));
                   inside.fetch_sub(1);
                 }));
    ASSERT_TRUE(w.has_value());
    Pool pool(io, 4);
    ASSERT_EQ(::write(b.get(), "012345678901234567890123456789", 30), 30);
    pool.Join();
    EXPECT_EQ(overlaps.load(), 0);
    EXPECT_EQ(got.load(), 30);
  });
}

TEST(IoSchedulerThreads, PeriodicTimerNeverOverlapsItself) {
  ForEachBackend([](IoScheduler &io) {
    Atomic<int> inside(0), overlaps(0), ticks(0);
    ASSERT_TRUE(io.RunEvery(Duration::FromMilliseconds(1), MakeTask([&] {
                              if (inside.fetch_add(1) != 0)
                                overlaps.fetch_add(1);
                              ticks.fetch_add(1);
                              SleepFor(Duration::FromMilliseconds(4));
                              inside.fetch_sub(1);
                            }))
                    .has_value());
    Pool pool(io, 4);
    SleepFor(Duration::FromMilliseconds(60));
    io.Stop();
    pool.Join();
    EXPECT_EQ(overlaps.load(), 0);
    EXPECT_GT(ticks.load(), 2);
  });
}

TEST(IoSchedulerThreads, WatchAddedFromAnotherThreadWhileLoopSleeps) {
  ForEachBackend([](IoScheduler &io) {
    Pool pool(io, 2); // sleeping in the operating system / on the condition
    SleepFor(Duration::FromMilliseconds(20));
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    Fd a(sv[0]), b(sv[1]);
    Atomic<int> hit(0);
    Expected<WatchId, CommonError> w =
        io.Watch(a.get(), IoEvent::Read, MakeCb([&](IoReady) {
                   hit.store(1);
                   io.Stop();
                 }));
    ASSERT_TRUE(w.has_value());
    ASSERT_EQ(::write(b.get(), "x", 1), 1);
    pool.Join();
    EXPECT_EQ(hit.load(), 1);
    EXPECT_TRUE(io.Unwatch(w.value()));
  });
}

TEST(IoSchedulerThreads, TimerAddedFromAnotherThreadWakesLeader) {
  ForEachBackend([](IoScheduler &io) {
    Pool pool(io, 1);
    SleepFor(Duration::FromMilliseconds(20));
    Stopwatch sw;
    ASSERT_TRUE(io.RunAfter(Duration::FromMilliseconds(10),
                            MakeTask([&] { io.Stop(); }))
                    .has_value());
    pool.Join();
    EXPECT_LT(sw.Elapsed().milliseconds(), 1000);
  });
}

TEST(IoSchedulerThreads, UnwatchWhileCallbackRunsOnAnotherThread) {
  ForEachBackend([](IoScheduler &io) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    Fd a(sv[0]), b(sv[1]);
    Atomic<int> started(0), finished(0);
    Expected<WatchId, CommonError> w =
        io.Watch(a.get(), IoEvent::Read, MakeCb([&](IoReady r) {
                   char c;
                   {
                     ssize_t n = ::read(r.fd, &c, 1);
                     (void)n;
                   }
                   started.store(1);
                   SleepFor(Duration::FromMilliseconds(30));
                   finished.store(1);
                 }));
    ASSERT_TRUE(w.has_value());
    Pool pool(io, 2);
    ASSERT_EQ(::write(b.get(), "x", 1), 1);
    while (started.load() == 0)
      SleepFor(Duration::FromMilliseconds(1));
    EXPECT_TRUE(io.Unwatch(w.value())); // returns while the callback runs
    EXPECT_EQ(finished.load(), 0);
    SleepFor(Duration::FromMilliseconds(50));
    EXPECT_EQ(finished.load(), 1);
    io.Stop();
    pool.Join();
  });
}
