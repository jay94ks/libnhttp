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

**Status: P0–P5 all done.** Combined result, loopback: 13.8K → 58.0K req/s (~4.2×); Docker
network-stack benchmark: 11.2K → 35.9K req/s (~3.2×) — nhttpd now clearly beats Apache in both and
sits at roughly half (loopback) to two-thirds (Docker) of nginx's throughput, up from a tenth and
a sixth respectively. Two items (P3, and P4's first two iterations) were implemented, measured,
and reverted rather than kept on the strength of theory alone — see their entries below for what
was tried and why it didn't hold up; that's not wasted effort, it's what "should be validated"
above actually means in practice.

## P0 — done (context, not further work)

- [x] Fix the `task<T>` coroutine-frame leak (`include/nhttp/async/task.hpp`).
- [x] Ignore `SIGPIPE` in `listener`'s constructor so an aborting client can't kill the process.

## P1 — a `sendfile`-based fast path for static files — done, measured

**Why this was first**: it was the single largest known gap. nginx serves a static GET via one
`sendfile()` syscall — zero userspace copies, no thread hop. nhttpd's old path (`overlay` →
`file_stream`/`range_stream` → `http1_io::write_message_body`) read each 4&nbsp;KB chunk into a
userspace buffer on the blocking thread pool, then wrote that buffer back out on the reactor
thread — two copies and a thread hop per chunk, for data the kernel could have moved directly
from the page cache to the socket.

**What shipped**: `http1_io::write_message_body` (`src/server/http1_io.cpp`) now takes a fast
path — plain POSIX `sendfile(2)` straight from the file descriptor to the socket descriptor —
whenever the response body is backed by a plain `io::file_stream` (checked via `dynamic_cast`;
a byte-`Range` response's `file_stream` is wrapped in a `range_stream`, which fails this cast and
correctly falls through to the generic path) and the wire is a plain `io::socket_stream` (checked
the same way — a `tls_stream`-wrapped connection also correctly falls through, since TLS has no
kernel-level sendfile equivalent) with a known, positive `content_length`. `io::file_stream`
gained `native_fd()` for this; `platform::socket_handle` gained `send_file()` +
`supports_send_file()` (the same "real capability check, not a guess" pattern `set_reuse_port()`
already used) and `async_socket` gained a `send_file()` coroutine using the exact same
would-block/retry contract as `write_some()`. **Windows (`TransmitFile`) was deliberately not
attempted this round** — real risk, not laziness: `TransmitFile` without `OVERLAPPED` either
blocks or needs the true IOCP completion model this reactor's plain reads/writes deliberately
don't use (see `CLAUDE.md`'s Phase 12 design note); wiring it in without being able to verify the
overlapped-completion edge cases as rigorously as everything else in this codebase risked
shipping something subtly wrong. `platform::socket_handle::supports_send_file()` returns `false`
on Windows, so every response there still takes the (fully correct, already-existing) generic
path — a real, documented platform difference, not a silently-missing feature.

**Verified**: `strace` on a real running server confirms the syscall actually fires
(`sendfile(37, 38, [0] => [10277], 10277) = 10277` for the benchmark's 10,277-byte file) and a
byte-for-byte `diff` against the served file matched exactly. Full suite green — 113/113 on Linux,
109/109 on Windows (109 not 113 there only because TLS is off on this dev machine, an existing,
unrelated gap — see `CLAUDE.md`'s Phase 12 log), both platforms warning-free.

## P2 — cut redundant thread-pool round trips per request — done, measured

Landed together with P1 and measured as one round (see the combined benchmark numbers below —
splitting the two apart would need a third benchmark run only to satisfy curiosity, not to decide
anything):

- `overlay::resolve()` and the equivalent in `single_file` now cache their result on the
  request's own tag storage (see `CONCEPTS.md`'s tag mechanism), keyed by the specific extension
  instance that computed it (`extension_registry::dispatch()` always calls `wants()` then
  immediately `handle()` on the *same* extension with nothing else running in between — see
  `src/server/extension.cpp` — so this is safe even with multiple `overlay`/`single_file`
  instances mounted on one listener). A request that reaches `handle()` after `wants()` already
  claimed it no longer stats the filesystem twice.
- `file_stream::open()` now does the `fopen` and the `ftell`/`fseek`/`ftell` size probe in one
  thread-pool hop instead of two, and takes an optional `known_size` that skips the probe's four
  syscalls entirely when the caller already has it from its own `stat()` — both `overlay` and
  `single_file` now pass their already-resolved `file_info::size` through.

**Combined P1+P2 result**: 13.8K → ~30K req/s on loopback, ~11K → ~24K req/s in the Docker
network-stack benchmark — nhttpd reached parity with Apache on both. P4 below pushed this further
still; see `ReadMe.md`'s Benchmarks section for the final combined tables (58K/36K req/s).

## P3 — reduce coroutine-frame allocation churn — tried, measured, reverted

With the P0 leak fixed, frames are correctly freed, but every `co_await` still does a fresh heap
allocation for the callee's frame (the standard `operator new` coroutine allocation path). The
theory: a request chains dozens of `task<T>`-returning calls, so this could be real allocator
pressure under sustained load.

**What was tried**: a thread-local, size-bucketed free-list allocator (`operator new`/`delete` on
`task_promise_base`, shared by every `task<T>` specialization) — implemented, and the full suite
stayed green (113/113) with the 2,000,000-await leak-repro still flat (~0 growth), so it was at
least *correct*. **Profiling it properly wasn't possible in this environment**: this machine's
WSL2 kernel (`6.6.87.2-microsoft-standard-WSL2`) has no matching `linux-tools` package for `perf`
to attach to, and `strace -c` (the fallback) is invasive enough under `ptrace` to drop this exact
workload's throughput by roughly 20× — not remotely representative of the hot path it was meant to
measure. In its place, a direct A/B benchmark of the real server (the same loopback wrk run used
throughout this document) was used as the evidence instead: **~31.4K req/s with the allocator vs.
~29.6–31.6K req/s without, across repeated runs — within normal run-to-run noise, no measurable
win.** The likely reason, in hindsight worth having considered before writing the code: glibc
2.26+ (this machine has 2.39) already ships a per-thread `tcache` doing almost exactly the same
thing (thread-local, size-classed, lock-free small-allocation reuse) as a built-in fast path — a
hand-rolled equivalent on top has little left to gain, and P1 additionally *shortened* the
`task<T>` chain for a static-file request (the sendfile path skips the whole
thread-pool-read/reactor-write loop's awaits entirely), so there's less to amortize than when this
item was first written. **Reverted** rather than kept for a non-result — `include/nhttp/async/
task.hpp` is back to the P0 fix only. Real coroutine-frame-allocator wins are a known technique in
general, just not a demonstrated one for this codebase's actual shape; worth retrying only with
real profiling data from an environment that can produce it (a native Linux host, not this WSL2
kernel), not from theory alone.

## P4 — `thread_pool`'s job queue — done, measured, three iterations

This item originally said "do not change this without profiling data showing it's actually a
bottleneck" — the user asked for it to be done anyway, so it was, and it turned into the most
instructive item in this whole plan: two attempts that looked reasonable on paper were
implemented, benchmarked, and found to actively regress throughput before the third one stuck.
Every iteration was verified correct first (full suite + ThreadSanitizer, including a new
`tests/unit/test_mpmc_queue.cpp` stress test — 8 producers × 4 consumers × 20,000 items each
through a queue deliberately smaller than the total, checking every item is delivered exactly
once) before ever being benchmarked, since a subtly-wrong concurrency primitive is a worse outcome
than a slow one.

**① `std::counting_semaphore` for wake signaling — measured ~2× slower, reverted.** Job storage
became a lock-free bounded MPMC ring buffer (`include/nhttp/async/detail/mpmc_queue.hpp`, Dmitry
Vyukov's well-known design), with a `std::counting_semaphore` replacing the mutex+condvar for
"wake a worker." This measured **13.8K → 15.8K req/s** — barely better than pre-P1/P2, and far
below the ~30K the plain mutex+`std::queue` this replaced was already getting. Root cause: a
semaphore's `acquire()`/`release()` pair is paid on *every single dequeue*, even by a worker that's
already busy and immediately finds another job waiting — the original mutex design let a "hot"
worker skip synchronization overhead entirely by just re-locking its own queue mutex on the way
back around, something per-item semaphore accounting can't do. `top` during the benchmark showed
58% system time (kernel/syscall) vs. an idle baseline of ~0% — consistent with far more
synchronization syscalls per request than before, not a busy-spin (which would show as user time).

**② lock-free queue + a mutex/condvar used *only* for event propagation — the shape that stuck,
but not before a second false start.** Keep the lock-free ring buffer for the actual push/pop data
path; add back a plain `std::mutex`/`std::condition_variable` solely to let an idle worker sleep
and be woken, guarded by the standard double-check-under-the-lock pattern so a push racing with a
worker about to sleep is never lost. A "hot" worker's loop is then pure lock-free `try_pop()` with
no synchronization primitive touched at all — closing exactly the gap semaphore design ① couldn't.
First benchmark of this shape, though, was **still only ~12.5K req/s at `blocking_pool_size = 64`**
— this plan's own earlier tuning advice, from when the generic read/write path needed many
concurrent blocking hops per request. The actual cause: 64 OS worker threads contending over this
machine's 8 CPU cores is a real, heavy oversubscription, and a CAS-retry-loop-based queue degrades
badly under it (a thread preempted mid-retry makes every other thread's retry work against a
moving target, where a blocking mutex just lets the OS park the idle thread instead of burning
cycles on it) — confirmed directly: the *same* mutex+`std::queue` design that scored ~30K–32K at
pool size 64 dropped to ~20K at pool size 8, the opposite trend, while this lock-free design went
from ~12.5K at pool size 64 up to **~45K at pool size 8 and ~69K at the library's own untouched
default of 4** — because P1's sendfile path means a request only ever needs two quick thread-pool
hops (stat + open) now, not the five-plus a naive implementation needed when this plan was first
written, so a large worker pool was never actually necessary to keep up. **Net result:
`blocking_pool_size` needs no manual tuning at all anymore — the shipped default (4) is the fastest
setting measured**, the opposite of this plan's own original P1 guidance.

**③ dropped the "is anyone waiting" gate — simpler, measured equivalent.** The first working
version of ② gated the wake-up (`enqueue()`'s notify) behind an atomic waiter count, skipping the
mutex/condvar entirely when nothing was asleep. Reviewing it raised a fair question: does the gate
actually earn its keep, given glibc's `condition_variable::notify_one()` already tracks waiters
internally and skips the underlying futex-wake syscall itself when there's nothing to wake — and
does the extra atomic load + branch on every `enqueue()` call risk its own branch-prediction cost?
A/B benchmark: ~66K–69K req/s with the gate vs. ~63K–68K without, across repeated runs — equivalent
within normal noise. Kept the simpler version (no `waiting_` counter, unconditional
`lock_guard` + `notify_one()`) — one fewer atomic, one fewer branch, same measured throughput.

**Verified**: 119/119 (Linux) / 115/115 (Windows, no TLS on this dev machine) across every
iteration, plus the full suite (160,457 assertions) clean under ThreadSanitizer for the final
shape — no data races in either the lock-free queue's CAS logic or the mutex-guarded wake path.

## P5 — a windowed, memory-mapped file read path — done, measured

`io::file_stream`'s read-only ("rb") opens can now memory-map the file instead of using buffered
`fread()` — see `platform::file_mapping` (`include/nhttp/platform/file_mapping.hpp`, POSIX
`mmap`/Windows `CreateFileMapping`+`MapViewOfFile`).

**Bounded windowing, not whole-file mapping.** A naive "map the whole file" implementation was
explicitly ruled out: a multi-gigabyte file mapped in its entirety just to serve a small
byte-`Range` request (or even a single sequential read of a huge file) wastes address space and
page-table setup at best, and could exhaust it outright on a 32-bit target. Instead,
`file_mapping::ensure_window(offset, length)` slides a single bounded (4&nbsp;MiB)
window across the file, remapping only when a request falls outside what's currently mapped — a
purely sequential reader (the normal case) ends up remapping roughly once per 4&nbsp;MiB, and any
file smaller than that (the overwhelming majority of real static files, including everything this
project's own benchmarks use) ends up mapped in one window covering the whole thing, same as an
unwindowed design would have done. Verified directly: a dedicated test
(`file_stream slides its mapped window correctly across a file bigger than one window` in
`tests/unit/test_file_stream.cpp`) builds a file spanning 3 windows plus change and checks
sequential reading, a seek back into an earlier window, and a seek forward skipping two windows at
once, all byte-exact.

**Lazy, not eager — found the hard way.** The first version opened the mapping eagerly inside
`file_stream::open()`, alongside the existing combined `fopen`+size-probe hop from P2. This
measurably *regressed* the common case: a whole-file GET never calls `read()` at all (P1's
sendfile path sends straight from `native_fd()`), so every one of those requests was paying for an
entirely unused second `open()`+`fstat()` pair. Benchmark caught it immediately — throughput
dropped back to roughly the pre-P1/P2 range at first (~10–16K req/s) despite P1's sendfile path
being fully intact and unaffected in the actual data-transfer sense; the extra file-open pair
alone accounted for the loss. Fixed by moving the mapping attempt into `read()` itself, gated on a
`mapping_attempted_` flag checked once per stream — reachable only by requests that actually call
`read()` (byte-`Range`, TLS, or `Transfer-Encoding: chunked` responses, none of which take the
sendfile path), so a whole-file GET pays exactly the same cost as before P5 existed at all. A
`seek()` issued before the first `read()` (i.e., before mapping is attempted) still goes through
the real `fseek()`-based path unchanged; the lazy mapping attempt reads the `FILE*`'s cursor via
`portable_ftell64()` in the same thread-pool hop that opens the mapping, so it picks up correctly
from wherever any prior seeking left off instead of silently resetting to byte 0.

**Verified**: full suite green on both platforms after each fix, including the dedicated windowing
test above; the eager-vs-lazy regression and its fix were each confirmed with the same loopback
`wrk` benchmark used throughout this document, not assumed from code review alone.

## Explicitly out of scope for this plan

- Anything already tracked separately: QUIC/HTTP-3 (see `CLAUDE.md`'s architecture decisions,
  point 8), ALPN-negotiated HTTP/2 over TLS, HTTP/2 `CONTINUATION`/server push (see `CLAUDE.md`'s
  Phase 14 log).
- Reverse-proxy connection pooling and active upstream health checks (`CLAUDE.md`'s Phase 13 log
  already lists these as known, reasonable follow-ups outside this round's scope).
- Rewriting `io_context`/`async_socket` around a native completion model instead of the current
  portable readiness-based contract (`CLAUDE.md`'s Phase 12 design note explains why that was
  deliberately avoided even for Windows/IOCP) — nothing here requires revisiting that.
