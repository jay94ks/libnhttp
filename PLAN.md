# Performance improvement plan

**Language**: English | [한국어](PLAN.ko.md)

Written after the benchmark round documented in `ReadMe.md`'s Benchmarks section, which measured
nhttpd well behind nginx and Apache serving a small static file under sustained load (loopback:
13.8K req/s vs. nginx's 139K and Apache's 38.6K; the same relative gap held in the
Docker network-stack benchmark). Two real correctness bugs found during that round are already
fixed on `main` (a systemic `task<T>` coroutine-frame leak and a `SIGPIPE` crash — see `ReadMe.md`
and `CLAUDE.md`'s Phase 15 log for the full story); this document is about the throughput gap that
remains after those fixes, which is a genuine architectural/implementation gap, not a bug.

Each item below is scoped, ordered by expected impact vs. effort, and should be validated by
re-running `benchmark/docker/`'s stack (the standard reproducible benchmark going forward) and
updating `ReadMe.md`'s Benchmarks section with the new numbers — the same way each phase in
`CLAUDE.md` closes with real measured verification, not just "should be faster."

## P0 — done (context, not further work)

- [x] Fix the `task<T>` coroutine-frame leak (`include/nhttp/async/task.hpp`).
- [x] Ignore `SIGPIPE` in `listener`'s constructor so an aborting client can't kill the process.

## P1 — a `sendfile`-based fast path for static files

**Why this is first**: it's the single largest known gap. nginx serves a static GET via one
`sendfile()` syscall — zero userspace copies, no thread hop. nhttpd's current path
(`overlay` → `file_stream`/`range_stream` → `http1_io::write_message_body`) reads each 4&nbsp;KB
chunk into a userspace buffer on the blocking thread pool, then writes that buffer back out on
the reactor thread — two copies and a thread hop per chunk, for data the kernel could have moved
directly from the page cache to the socket.

**Plan**:
- Add a narrow fast path in `static_content.cpp`/`connection.cpp`'s response-writing path: when
  the response body is backed by a plain `file_stream` (no `Range`, no chunked/transfer coding,
  no TLS on the connection — TLS has no kernel-level sendfile equivalent) and the platform
  supports it, use `sendfile(2)` (Linux) directly from the file descriptor to the socket
  descriptor instead of the generic `read`/`write` loop.
- Windows equivalent: `TransmitFile`, which already fits this codebase's IOCP-based reactor
  (overlapped, event-driven) — a natural pairing with the work done in `CLAUDE.md`'s Phase 12.
- Keep the existing generic path as the fallback for TLS connections, byte-`Range` requests
  (partial-file sendfile is still possible via the syscall's offset/count args, worth doing once
  the simple case works), and non-file streams (`reverse_proxy`, generated responses, etc.) —
  this is additive, not a replacement for `io::stream`'s general contract.
- This alone should close most of the gap for the specific micro-benchmark measured (a small
  static file, repeatedly) — worth remeasuring before doing anything below.

## P2 — cut redundant thread-pool round trips per request

Independent of P1 (still needed for non-sendfile-eligible responses, and for the metadata work
that precedes any response):

- `overlay::wants()` and `overlay::handle()` both call `resolve()`, which independently
  `stat()`s the same path — a known, already-logged tradeoff from `CLAUDE.md`'s Phase 8
  ("not worth optimizing away yet ... would need a request-tag cache"). It's worth it now: cache
  the resolved `(fs_path, file_info)` on the request via its tag storage (see
  `CONCEPTS.md`'s tag mechanism) so a request that reaches `handle()` after `wants()` claimed it
  doesn't stat the filesystem twice.
- `file_stream::open()` does two separate `pool_->run()` hops — one for `fopen`, one for the
  initial `ftell`/`fseek`/`ftell` size probe. Combine them into a single thread-pool job that
  opens and sizes the file in one hop; halves the thread-pool round trips for every file open.
- Consider whether `overlay`'s `stat_file()` result and `file_stream::open()`'s own size probe
  can share one thread-pool hop entirely (the size is already known from the first `stat`) —
  would remove a third round trip, at the cost of trusting the size doesn't change between the
  two calls (acceptable: the existing code already has this same race implicitly, since nothing
  re-validates size between `resolve()` and actually reading the file).

## P3 — reduce coroutine-frame allocation churn

With the P0 leak fixed, frames are now correctly freed, but every `co_await` still does a fresh
heap allocation for the callee's frame (the standard `operator new` coroutine allocation path).
A request chains dozens of `task<T>`-returning calls, so this is real allocator pressure under
sustained load, independent of the leak.

- Profile first (this needs actual data, not a guess — `perf record`/`perf report` under the
  Docker benchmark's load would show whether allocator time is actually significant relative to
  the thread-pool round trips P1/P2 target).
- If it is: `task_promise<T>`/`task_promise<void>` can supply their own `operator new`/`delete`
  backed by a size-classed free-list allocator (a well-known coroutine optimization — frame sizes
  for a given coroutine function are fixed per-instantiation, so a simple per-size free list, not
  a general allocator, is enough). This is a contained change to `include/nhttp/async/task.hpp`
  only.

## P4 — revisit `thread_pool`'s queue if it becomes the bottleneck

`async::thread_pool` (`src/async/thread_pool.cpp`) is a single `std::mutex` + one
`std::condition_variable` + one `std::queue` shared by every worker thread and every submitting
coroutine. At `blocking_pool_size` values large enough to matter under real load (this benchmark
round used 64), contention on that one mutex is plausible but **not yet measured** — do not
change this without profiling data showing it's actually a bottleneck relative to P1/P2, since a
sharded or lock-free queue is real complexity to introduce into a currently simple, correct
component.

## Explicitly out of scope for this plan

- Anything already tracked separately: QUIC/HTTP-3 (see `CLAUDE.md`'s architecture decisions,
  point 8), ALPN-negotiated HTTP/2 over TLS, HTTP/2 `CONTINUATION`/server push (see `CLAUDE.md`'s
  Phase 14 log).
- Reverse-proxy connection pooling and active upstream health checks (`CLAUDE.md`'s Phase 13 log
  already lists these as known, reasonable follow-ups outside this round's scope).
- Rewriting `io_context`/`async_socket` around a native completion model instead of the current
  portable readiness-based contract (`CLAUDE.md`'s Phase 12 design note explains why that was
  deliberately avoided even for Windows/IOCP) — nothing here requires revisiting that.
