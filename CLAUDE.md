# CLAUDE.md

**Language**: English | [한국어](CLAUDE.ko.md)

Reference for any Claude Code session working in this repository. Read this before touching
build files, architecture, or the legacy tree.

## What this repo is

`libnhttp` is being **completely rewritten from scratch** as a C++20, coroutine-based,
multi-threaded, event-driven HTTP server library. This is not a refactor or incremental
modernization of the existing C++17 code under `libnhttp/`, `libnhttp-tests/`, `nhttpd/` — that
tree is the *previous* implementation and is left untouched on disk only as a reference until
the new implementation reaches feature parity, at which point it will be deleted (with the
user's explicit confirmation — never delete it unprompted).

Two documents record what carries forward from the old implementation and why:

- [`CONCEPTS.md`](CONCEPTS.md) — the design philosophy and invariants worth preserving (the
  event-driven/streaming model, the two-phase extension contract, the router's trie priority
  semantics, tag-based per-connection/per-request metadata, etc.), plus an explicit list of
  gaps the old implementation had (multipart/form-data was never implemented, WebSocket only
  had a handshake, no TLS) and naming/build-macro mistakes not to repeat.
- [`USAGE.md`](USAGE.md) — the observable API usage patterns from the old implementation
  (bootstrapping, routing, static file serving, vhosts, the manual curl-based smoke-test
  matrix) that any new API design should still be able to express, even with different
  signatures/names.

**No code is ported from the old tree.** Only the feature set and philosophy in those two docs
carry forward. If you need to know "how did the old code do X," read `CONCEPTS.md`/`USAGE.md`
first — they're the curated summary; only dig into `libnhttp/nhttp/**` directly for something
those two docs don't cover.

A third document looks forward instead of back: [`PLAN.md`](PLAN.md) is a live to-do list of open
performance follow-ups, started after Phase 15's benchmark round (see that phase's entry below)
found nhttpd well behind nginx/Apache serving static files. It only ever tracks *unfinished* work
— an item is deleted from it once done, and its full story (what was tried, measured, kept, or
reverted) moves permanently into this file's phase log instead (see Phase 16). If you're picking
up performance work on this repo, check `PLAN.md` first for what's still open, and this file's
phase log for the history behind what's already done.

The full redesign plan (architecture + phased build order) lives at
`C:\Users\jay94\.claude\plans\dazzling-fluttering-badger.md` on the machine this was planned on
— it won't be present in a fresh clone, so the summary below is the durable record.

## Architecture decisions (confirmed with the user — do not silently relitigate these)

1. **C++20**, not 17. Coroutines are used for the entire async I/O model.
2. **CMake** build system, replacing the old `Makefile`/`.vcxproj`/`.sln`. ~~Linux-first: Windows
   support is deliberately deferred~~ — **superseded**: Windows (IOCP-based) support was added
   in Phase 12 (see the progress log below), on top of exactly the clean platform-boundary seam
   this decision called for — `include/nhttp/platform/**` needed hardening (see Phase 12's
   entry) but nothing above `src/platform/` changed.
3. **Concurrency**: a coroutine `task<T>` type + an `io_context` (one epoll instance + ready
   queue + timer heap) + an `io_context_pool` of N such contexts, each pinned to its own OS
   thread. New connections are load-balanced across worker threads via **`SO_REUSEPORT`**
   (each worker binds its own listening socket on the same address:port; the kernel balances
   accepts) rather than manual work-stealing. A connection's coroutine stays pinned to the
   thread that accepted it for its whole lifetime — no cross-thread synchronization needed for
   connection-local state. A separate `thread_pool` exists only for genuinely blocking work
   (filesystem stat/read) that epoll can't cover. **The reactor thread must never block**
   waiting on a mutex/condvar for a worker — this is the one non-negotiable invariant from the
   old implementation's concurrency contract (see `CONCEPTS.md` §2).
4. **Testing**: Catch2 (v3, via CMake `FetchContent`), replacing the old print-only
   `test_case` class. Unit tests per module plus real-socket integration tests on loopback
   (both `127.0.0.1` and `::1` — dual-stack is a first-class requirement, not optional).
5. **Router** (successor to the old `xfwk` namespace/codename — renamed, proposed
   `nhttp::router`): must port the trie-matching priority rule **exactly** — static child →
   deepest-matching parameter child → wildcard — because a real bug in the old implementation
   (commit `61a8fa1`, see `CONCEPTS.md` §5) was caused by getting this tie-break wrong. Any
   change to the router's matching logic must be checked against a regression test asserting
   this exact priority order before being considered correct.
6. **Multipart/form-data parsing and WebSocket frame send/receive are implemented for real**
   this time — the old implementation only stubbed these (form-data had no parser at all;
   WebSocket only completed the HTTP Upgrade handshake, not actual frame I/O).
7. ~~TLS/SSL is out of scope for this round~~ — **superseded**: TLS/SSL was implemented in
   Phase 11 (see the progress log below), exactly on top of the stream/transport abstraction
   this decision called for keeping swappable. The abstraction did not need to change.
8. ~~HTTP/2 and QUIC are not implemented~~ — **partially superseded**: HTTP/2 was implemented in
   Phase 14 (see the progress log below), directly on the three seams this decision called for
   preserving — all three held with zero changes needed, confirmed in Phase 14's entry.
   **QUIC stays explicitly deferred** — the user considered vendoring a QUIC implementation vs.
   a from-scratch RFC 9000/9114/9204 stack and chose to defer entirely rather than either
   (a from-scratch implementation was judged unrealistic to get interoperably correct in this
   kind of session; vendoring would break this project's zero-third-party-code stance). The
   three seams: (a) a connection/exchange split — router and handler code must never assume one
   request per connection; (b) a transport-agnostic async stream abstraction — drivers must talk
   to the network only through it, never assume a raw TCP socket; (c) headers reach the
   router/extensions as decoded key/value pairs, never as raw wire bytes, so HPACK/QPACK-decoded
   headers are indistinguishable from HTTP/1.1 ones at that layer. These three still stand as the
   seam QUIC/HTTP-3 would need whenever that round happens.
9. **Warning-free build** (`-Wall -Wextra -Wpedantic`, treated as errors) is a hard requirement,
   not aspirational — don't add code that needs warnings suppressed to compile.

## Directory layout

```
CMakeLists.txt              top-level build config
cmake/                      CMake helper modules (compiler warnings, etc.)
include/nhttp/              public headers (mirrors src/ module layout)
src/
  platform/                 portable public API (address/socket/reactor/file_info) + posix/win32
                             subdirs implementing it (epoll vs IOCP, POSIX vs Winsock) (Phase 12)
  async/                    task<T>, io_context, io_context_pool, socket awaitables, timers,
                             thread_pool (blocking-work offload)
  io/                       async stream interface + memory/file/range/socket streams
  protocol/                 header/method/status/mime/date/query_string/resource/urlencode,
                             chunked codec, multipart/form-data parser
  server/                   listener, HTTP/1.1 connection coroutine, connection_h2 (HTTP/2,
                             Phase 14), shared http1_io.* framing helpers, request/response
                             facade, tag storage, params/config
  server/extensions/        extension registry, vhost, vpath, static overlay, single-file
                             serving, reverse_proxy (Phase 13)
  router/                   trie-based router (facade/route/middleware/target), successor to xfwk
  ws/                       WebSocket handshake + RFC6455 frame codec + async send/recv
  tls/                      TLS/SSL via OpenSSL (tls_context, tls_stream) — memory-BIO pattern,
                             implements io::stream so it's a drop-in transport; server *and*
                             client mode (Phase 11, client mode added in Phase 13)
  http2/                    HPACK (RFC 7541) + frame codec (RFC 9113) — no QUIC/HTTP-3 (Phase 14)
  depends/                  vendored third-party (sha1, utf8) — reused as-is unless trivially
                             replaceable by a standard facility
tests/
  unit/                     one file per module, Catch2
  integration/              real listener on loopback, driven by a small test HTTP/WS/HTTP2 client
examples/
  nhttpd/                   sample app mirroring the old demo endpoints (written in the final phase)
libnhttp/, libnhttp-tests/, nhttpd/, coverage/, Makefile, *.vcxproj, *.sln
                             LEGACY — old C++17 implementation, untouched, reference-only,
                             deleted only after new implementation reaches parity + user confirms
```

## Build & test — Linux (WSL Ubuntu when the host is Windows)

This machine's WSL Ubuntu instance has GCC 13.3.0, CMake 3.28, and Ninja pre-installed. If
invoking from a Windows shell, prefix commands with `wsl.exe -d Ubuntu -- bash -lc "..."`; paths
under `C:\GitHub\libnhttp` are reachable inside WSL at `/mnt/c/GitHub/libnhttp`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Build & test — Windows (native, since Phase 12 — no longer WSL-only)

This machine has Visual Studio 18 Community (MSVC `cl` 19.51+) and CMake/Ninja installed
natively. From a native Windows shell (PowerShell/cmd, **not** WSL), load the MSVC environment
first, then configure/build/test exactly as on Linux — `NHTTP_ENABLE_TLS=OFF` is required here
specifically on this machine (see Phase 12's progress-log entry: no MSVC-linkable OpenSSL dev
package is installed, a build-environment gap, not a design one):

```bash
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNHTTP_ENABLE_TLS=OFF
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

(`vcvars64.bat`'s environment doesn't reliably propagate through inline PowerShell one-liners in
this setup — put the above in a `.bat` file and run that, rather than trying to chain it inline.)

Every phase of work must leave the tree in a state where the build+test commands succeed with
zero compiler warnings, **on both platforms**, before moving on.

## Coding conventions

- No raw `new`/`delete`; ownership expressed via `std::unique_ptr`/`std::shared_ptr` or scoped
  RAII types.
- Prefer `std::string_view`/`std::span` over copying where the original implementation copied
  purely because C++17 lacked a non-owning view.
- Use standard library primitives (`std::mutex`, `std::condition_variable`, `std::jthread`,
  `std::atomic`) directly rather than reinventing wrapper types, unless there's a concrete,
  documented reason a standard facility doesn't fit (if you hit one, note it here).
- Coroutine-returning functions (`task<T>`) never block their calling thread; if something must
  block a real OS thread (filesystem I/O), it goes through the `thread_pool`, not inline in a
  reactor-thread coroutine.
- Public headers under `include/nhttp/` should stay free of implementation detail leakage where
  reasonably possible (pimpl or clean interface types), mirroring the source layout under `src/`.

## Progress log

- **Phase 0 (scaffolding) — done.** CMake skeleton, `cmake/CompilerWarnings.cmake`, Catch2 via
  FetchContent, `nhttp` library target, `nhttp_unit_tests` target wired to CTest.
- **Phase 1 (platform + async core) — done.** `src/platform/` (address, socket, epoll wrappers)
  and `src/async/` (`task<T>`, `detached_task`, `sync_wait`, `io_context`, `io_context_pool`,
  `thread_pool`, `async_socket`) are implemented and covered by unit/integration tests
  (loopback accept/connect/read/write through a real epoll reactor, timers, cross-thread
  `post()`, thread-pool offload, multi-context pool). All 14 tests pass warning-free.
  - **Verified with ThreadSanitizer** (see below) — caught and fixed one real race in
    `sync_wait_event::set()` (notify-after-unlock let a waiter destroy the condvar while the
    notifier was still inside `pthread_cond_broadcast`; fixed by notifying while still holding
    the lock, in `include/nhttp/async/sync_wait.hpp`).
  - **Design note for future async code touching a live `io_context`**: an `io_registration`'s
    fields and an `io_context`'s timer heap are *not* synchronized — they're only safe to touch
    from the single thread running that context's `run()` loop. The safe pattern (used
    throughout `tests/unit/test_io_context.cpp` and `test_io_context_pool.cpp`) is to spawn a
    coroutine chain (reach its first suspension point) *before* that context's reactor thread
    exists, so the first registration is single-threaded; every resumption after that happens
    from inside `run()`'s own dispatch, i.e. already on the right thread. Only `post()`/`stop()`
    (and plain `std::atomic`s) are safe to touch from a foreign thread once the reactor is
    running. This is exactly the "connection pinned to the thread that accepted it" invariant
    from the architecture decisions above — it's not just a performance choice, it's required
    for correctness given `io_registration`/timer state have no locks.
  - **Running ThreadSanitizer on this machine's WSL2 Ubuntu requires `setarch $(uname -m) -R`**
    before the binary (e.g. `setarch $(uname -m) -R ./build-tsan/tests/nhttp_unit_tests`) — a
    plain invocation fails immediately with `FATAL: ThreadSanitizer: unexpected memory mapping`
    due to WSL2's default ASLR layout; this also breaks CTest's `catch_discover_tests` post-build
    step (which invokes the binary directly), so build a separate `-fsanitize=thread` CMake
    build directory and run its test binary directly with `setarch`, not through `ctest`.
- **Phase 2 (async streams) — done.** `src/io/`: `stream` (async read/write/seek/close +
  shared `read_all`), `memory_stream`, `range_stream` (byte-window decorator, unbounded
  pass-through fallback), `socket_stream` (wraps `async_socket`), `file_stream` (offloads via
  `thread_pool`, per the "reactor thread never blocks" invariant). 24/24 tests pass warning-free.
  - **Catch2 CMake-discovery gotcha**: a `TEST_CASE` NAME containing literal `[`/`]` characters
    (e.g. describing a `[begin, end)` range in prose) can make `catch_discover_tests`'s
    post-build parser concatenate that test with several following ones into one bogus CTest
    entry — the test binary itself is fine (`--list-tests` shows correct separate names); it's
    the discovery script tripping on the brackets. Avoid brackets in test **names** (tags in the
    second argument, `"[io][range_stream]"`, are fine — only the prose name is the hazard).
- **Phase 3 (HTTP/1.1 protocol parsing + multipart/form-data) — done.** `src/protocol/`:
  `http_method` (semantic flags as data, per CONCEPTS.md), `http_header`/`http_headers`
  (incremental line parser, case-insensitive lookup), `http_status` (reason-phrase table),
  `urlencode`, `http_query_string`, `http_date` (RFC 1123, via `timegm`/`gmtime_r` — simpler
  than the original's localtime-delta hack), `http_mime_type` (+ extension table + shared
  `parse_header_parameters` used by both MIME params and multipart Content-Disposition),
  `http_resource` (request-line parser), `http_chunked` (`chunked_decoder_stream` + chunk
  header formatting), and `http_multipart` (`multipart_reader`/`multipart_part`) — the
  streaming, memory-bounded multipart/form-data parser that the original implementation never
  actually had (see CONCEPTS.md §6). 63/63 tests pass warning-free.
  - **Gotcha carried over from an anonymous-namespace friend bug found in this phase**: in
    `src/protocol/http_multipart.cpp`, `multipart_part_stream` must NOT be placed in an
    anonymous namespace, because `multipart_reader`'s `friend class multipart_part_stream;`
    names that class in the enclosing `nhttp::protocol` namespace specifically — an
    anonymous-namespace class of the same name is a distinct type and the friendship silently
    doesn't apply (compiles fine right up until the friend-only member is actually called).
  - **Test-writing gotcha**: inside a `.cpp` that does `using namespace nhttp::io;` (etc.)
    without `using namespace nhttp;`, writing `io::stream` does NOT work — `using namespace`
    only pulls in the *members* of a namespace unqualified, not a usable alias for the
    namespace's own name. Use the bare unqualified name (`stream`), not `io::stream`.
- **Phase 4 (server core) — done.** `src/server/`: `params` (expanded multi-threaded config),
  `tag_storage` (type-erased per-type slots — the tag mechanism from CONCEPTS.md §1, used both
  connection-scoped (`connection::tags()`) and request-scoped (`request::tags`)), `request`/
  `response` facades + `make_response(...)` free functions, `connection` (the HTTP/1.1
  read-dispatch-write loop as one coroutine — no state-machine enum, see CONCEPTS.md §2), and
  `listener` (owns an `io_context_pool`, binds one `SO_REUSEPORT` socket per worker per
  endpoint, `run()`/`stop()` block/unblock the calling thread). A single settable
  `handler_type` (`std::function<task<response>(request&)>`) is the terminal dispatch target
  for now — Phase 5 replaces/wraps this with the full priority-ordered extension registry.
  70/70 tests pass warning-free, including `tests/integration/test_server_basic.cpp` — a real
  `listener` driven end-to-end by a small hand-rolled blocking HTTP/1.1 client (deliberately
  independent of nhttp's own protocol/ code, so a shared bug wouldn't hide a failure): plain
  GET, POST with Content-Length, chunked request decoding, chunked response encoding (unknown
  body length), 404, keep-alive across two requests on one connection, and IPv6 loopback.
  - **Real bug found via this integration test, fixed in `range_stream`**: its constructor
    used to fall back to an unbounded pass-through whenever the *inner* stream's length was
    unknown (`get_length() < 0`), regardless of whether the caller had passed a real
    `[begin, end)`. That's correct for the original "no known range, just pass everything
    through" case, but wrong for using `range_stream` to impose an exact byte-count limit
    (e.g. an HTTP Content-Length body) over a live, non-seekable connection stream whose own
    length can never be known — the fallback was silently discarding the caller's requested
    bound and streaming forever, which hung the very first POST-with-body integration test.
    Fixed: `bounded_` is now decided purely by whether `begin`/`end` are non-negative (the
    caller's actual intent); the inner length, when known, only *clamps* that bound, it no
    longer overrides it. See `src/io/range_stream.cpp` for the fix and the reasoning comment.
  - **Known Phase-4 simplifications, not yet addressed**: no per-request timeout enforcement
    (`params::header_timeout`/`idle_timeout` are defined but not yet wired to anything — needs
    a "race read against a timer" combinator that doesn't exist yet); `Connection`/
    `Transfer-Encoding` header value matching is a simple case-insensitive substring check, not
    full token-list parsing (fine for `close`/`keep-alive`/`chunked` in practice, but not
    spec-strict for pathological multi-value headers).
- **Phase 5 (extensions) — done.** `src/server/extension.hpp`/`.cpp`: the two-phase
  `wants`/`handle` contract + priority-ordered `extension_registry`, now wired into `listener`
  (`listener::extends(...)`; the registry is tried before the single fallback `handler_type`).
  `src/server/extensions/`: `vpath` (the URL-prefix scoping primitive — `subpath_of(req)` reads
  a per-request `vpath_tag` stack pushed/popped around nested dispatch; the router in Phase 6
  is built on this same primitive, not a separate one, per CONCEPTS.md §4-5), `vhost`
  (hostname/regex/predicate-matched scoping, same shape as vpath but keyed on `request::
  hostname`), `static_content` (the **shared** conditional-GET + byte-Range engine —
  `serve_stream_conditionally`, `make_etag`, `qualify_relative_path` — used identically by both
  `overlay` (directory serving, falls back to an index file) and `single_file` (one fixed file
  at any matched path), so this logic is never duplicated between them). 75/75 tests pass
  warning-free, including real end-to-end coverage of: index-file fallback, named-file serving,
  404, **path-traversal rejection** (`qualify_relative_path` correctly blocks `/../../etc/...`,
  returning 403), ETag/If-None-Match → 304, byte-Range → 206 with correct `Content-Range`,
  vhost dispatch by `Host` header, and vpath-scoped nested overlay mounting.
  - `qualify_relative_path` (in `static_content.hpp`) is the one shared path-normalizer so far;
    Phase 6's router will want the same "resolve `.`/`..`, reject escapes" logic for its own
    path handling — reuse this rather than re-deriving it, per CONCEPTS.md's centralized-path-
    qualification principle, adjusting its home if the router needs a different shape (segment
    popping, not one-shot normalization).
- **Phase 6 (router) — done.** `src/router/`: `route`/`route_kind` (the four-node-kind path trie —
  static/param/wildcard/root — with the exact static→deepest-param→wildcard priority and
  capture rollback from CONCEPTS.md §5), `target`/`target_by(...)` (lambda and bound-member-
  function targets), `middleware`/`middleware_stack` (chain-of-responsibility via real closures
  — no tag-stack workaround needed, unlike the original, since coroutines + `shared_ptr` give
  safe capture semantics directly), `facade`/`group_proxy` (the fluent DSL + the "batch
  middleware over every route touched inside a `group()` body" ergonomic), and `router` (the
  extension tying the tree into `server::vpath`'s scoping, `route_of(req)` for retrieving
  captures). Renamed from "xfwk" — no meaning to carry forward. 85/85 tests pass warning-free,
  including `tests/unit/test_router_route.cpp` (trie logic directly, per the plan) and two
  integration suites through a real listener.
  - **The historical bug's regression test is `test_router_route.cpp`'s
    "routing picks the deepest matching parameter branch, not the first one tried"** — it
    encodes the exact shipped scenario (two parameter branches accept the same segment; one
    reaches a full match via a shallow wildcard, the other via two further static segments) and
    asserts the deeper one wins in **both** registration orders, which is what actually catches
    an order-dependent regression. **Verified this test is load-bearing**: temporarily replaced
    the fix's `!have_best || trial.depth > best_state.depth` comparison with an
    always-take-the-last-match rule (simulating a plausible reintroduction of the historical
    bug) and confirmed the test fails (wrong branch picked in the "`:y` registered before `:x`"
    ordering) before reverting — don't skip this kind of check when touching `route_match`.
  - **Design note**: this rewrite's fix is expressed differently from the original's (an
    explicit `have_best` flag that always accepts the first successful candidate as a baseline,
    rather than the original's "prime `deep_state.depth` one below the current depth" trick) —
    same semantics, more obviously correct, less fragile to get subtly wrong again.
  - A router mounted at `"/"` (or any prefix) is authoritative for everything under that
    prefix: a request whose path matches the mount but no registered route returns the
    router's own 404 (via `vpath::handle`'s "not handled = 404" fallback), it does **not**
    fall through to the listener's other extensions or its terminal handler. This is correct,
    intentional scoping behavior, not a bug — a test in `test_router_server.cpp` initially
    asserted the opposite (501) and had to be corrected.
- **Phase 7 (WebSocket) — done.** `src/ws/`: a self-written, dependency-free `sha1()` (FIPS
  180-1 — vendoring a third-party file wasn't worth it for ~70 lines) + base64 encode for the
  handshake (`compute_accept_key`, verified against the RFC 6455 §1.3 worked example), and
  `ws_connection` — a **real** RFC 6455 frame codec: fin/opcode, 7/16/64-bit payload lengths,
  masking (server always unmasks incoming, never masks outgoing, per spec), fragmented-message
  reassembly, and automatic ping→pong / close-handshake handling. This is the feature the
  original implementation only ever stubbed (handshake worked, frame I/O didn't — CONCEPTS.md
  §6) — this round it's fully implemented and tested (96/96 tests pass warning-free), including
  a real end-to-end handshake + echo test over actual sockets
  (`tests/integration/test_websocket_server.cpp`) using a hand-rolled client independent of
  nhttp's own `ws::` code.
  - **Protocol-upgrade mechanism, new in this phase**: `server::response` gained an
    `upgrade_handler` field (`std::function<task<void>(shared_ptr<io::stream>, std::string)>`).
    When an extension sets it (see `websocket_endpoint::handle`), `connection::write_response`
    sends only the status line + the extension's own headers (no automatic Content-Length/
    Transfer-Encoding/Connection), then moves its own `wire_` into a fresh heap-allocated
    `io::socket_stream` and hands that — plus whatever's left in `read_buffer_` (bytes already
    read past the HTTP request, if any) — to the handler, and `connection::run()` stops its own
    HTTP loop (`upgraded_` flag) instead of continuing to the next keep-alive request. This is
    the concrete mechanism behind CONCEPTS.md's "driver replacement" idea, and is the seam any
    future protocol upgrade (not just WebSocket) would reuse.
  - `websocket_endpoint`'s `on_connect` handler owns the WHOLE connection lifetime as a loop
    calling `co_await ws->receive()` until it returns `nullopt` — deliberately not a callback
    style (`on_message`/`on_disconnect`) like the original's stubbed `http_websocket`, since a
    plain loop is far more natural once real coroutines are available.
- **Phase 8 (protocol-extensibility validation) — done.** `docs/protocol-extensibility.md`
  reviews Phases 4–7 against the three HTTP/2-/QUIC-readiness seams from CLAUDE.md's
  architecture decisions. Two of three held with no changes; one real gap was found and fixed:
  `server::connection` used to take/store a concrete `io::socket_stream` rather than an
  abstract `shared_ptr<io::stream>`, which would have blocked substituting a future non-TCP
  transport without changing `connection`'s own signature. Fixed (`connection.hpp`/`.cpp`,
  `listener.cpp`) — `listener::handle_connection` is now the only place that still knows the
  wire happens to be a TCP `socket_stream`; this also removed a redundant re-wrap that used to
  happen on every WebSocket upgrade. 96/96 tests still pass unchanged after the fix (it's a
  non-behavioral type change). See the doc for the full per-seam findings.
  - **Second fix from the same review pass, in `overlay`**: `overlay::wants()` used to return
    `true` unconditionally for GET/HEAD, deferring the real existence check to `handle()` —
    meaning overlay always "claimed" every GET, and a request for a nonexistent file got
    overlay's own 404 with **no fallthrough to any other extension**, even one with lower
    priority. That's backwards for the common case of layering a router and a static-file
    overlay on the same mount point (a request for a nonexistent static file should fall
    through to the router/other extensions before finally 404ing/501ing). Fixed:
    `overlay::wants()` now actually resolves the path (`overlay::resolve`, shared with
    `handle()`) and only claims requests for files that genuinely exist — except a
    path-traversal attempt, which it still claims unconditionally so it can give a definitive
    403 rather than silently letting some other extension see the escaped path. This does mean
    a successful request stats the file twice (once in `wants()`, once in `handle()`); not
    worth optimizing away yet (would need a request-tag cache) at this scale.
- **Phase 9 (example app) — done.** `examples/nhttpd/main.cpp` (+ `examples/CMakeLists.txt`,
  built automatically when `NHTTP_BUILD_EXAMPLES` is on — reconfigure with `cmake -S . -B build`
  once after adding a *new* CMakeLists.txt anywhere, ninja won't notice it otherwise) mirrors
  the original demo endpoints from USAGE.md on the new API: `overlay` for static files,
  `websocket_endpoint` for a `/ws` echo, and a `router` with `/whoami`, `/always-501`, `/exit`
  (calls `listener::stop()`), and the `:user/profile|greetings|set` group with a `jay`/`kay`
  `param()` predicate. Takes an optional directory and port argument
  (`./nhttpd [dir] [port]`). **Manually verified against a real running instance with curl**
  (not just the automated suite): static index/file serving, byte-Range (`206` with correct
  `Content-Range`), the router's param/group/promoted-method routes, the `overlay`→router
  fallthrough for a nonexistent static path landing on the router's own 404, `/always-501`, and
  a clean shutdown via `POST /exit`.
  - **A real, load-bearing bug was found and fixed while writing this example**: `overlay`/
    `single_file` used to take a **fixed `io_context&` at construction time** and resume all
    their `thread_pool` file-I/O offloads on it — but a listener runs many worker io_contexts,
    and a connection is pinned to whichever one accepted it (CLAUDE.md's SO_REUSEPORT decision).
    If the fixed context happened to differ from the one actually running a given connection,
    resuming there would silently violate that pinning: the coroutine would continue running a
    request on the *wrong* thread, touching that connection's socket/`io_registration` state
    without the synchronization the design assumes only the owning thread needs. Existing tests
    didn't catch it (too few concurrent connections across too few workers to reliably land on
    a mismatched thread). Fixed by adding `request::io_ctx` (set to the connection's own context
    by `connection::run()` on every request) and having `overlay`/`single_file` resume on
    `*req.io_ctx` instead of a stored one — their constructors no longer take an `io_context&`
    at all, only a `thread_pool&` (call sites: drop the context argument from `overlay_of(...)`/
    `file_of(...)`/`overlay(...)`/`single_file(...)`). **Any future extension that offloads work
    to `thread_pool::run` must resume on `req.io_ctx`, never a context captured at the
    extension's own construction time** — this is now the load-bearing rule for that pattern.
  - **A related, non-bug but important interaction documented in the example's own comments**:
    a `router` (or any `vpath`) mounted at `"/"` has a `wants()` that trivially accepts every
    request (prefix-matching `"/"` always succeeds), so it must be given a *higher* priority
    number (lower precedence, tried later) than anything that should get first refusal — e.g.
    a static-file `overlay` or a `websocket_endpoint`. The numeric defaults
    (`vpath`/`router` = `0x80000000`, `overlay` = `0xE0000000`) do **not** guarantee this by
    themselves if you mount a "/"-rooted router; the example passes overlay an explicit lower
    priority (`0x10000000`) to get correct layering. Worth a bigger think before Phase 10's
    `ReadMe.md` rewrite: whether the *defaults* should change, or whether this should just be
    documented prominently (current lean: document it — changing defaults now would be a second
    silent behavior change on top of the `overlay::wants()` fix already made this session).
- **Phase 10 (final cleanup) — done.** `ReadMe.md` rewritten for the new API/build instructions
  and design-doc links. User explicitly confirmed deleting the legacy tree; removed via
  `git rm -r` (staged, not committed — the user hasn't asked for a commit):
  `libnhttp/`, `libnhttp-tests/`, `nhttpd/`, `coverage/`, `nhttpd.sln` (524 files). `benchmark/`,
  `bench1.jpg`, `bench2.jpg`, `logo.png` were deliberately left alone — out of the confirmed
  cleanup scope (old JMeter benchmark assets/images, not legacy code). Full suite (96/96)
  verified green after the removal. **This completes the redesign** — all 10 planned phases
  done: async core, streams, HTTP/1.1 protocol (incl. multipart), server core, extensions,
  router (with the priority-bug regression test verified load-bearing), real WebSocket frame
  I/O, a protocol-extensibility review (two fixes found and applied), an example app manually
  verified live with curl, and this cleanup.
  - **Unrelated anomaly noticed and flagged during this phase, not caused by this session's
    tracked work**: `LICENSE` was already modified on disk (a `Copyright (c) 2021 neurnn corp`
    line removed) with no corresponding edit in this session's history. Surfaced to the user
    explicitly rather than silently committing or reverting it blind; user confirmed keeping
    the current (line-removed) state.
- **Phase 11 (TLS/SSL) — done.** Added after Phase 10, at the user's explicit request to
  implement the gaps the original implementation had (`CONCEPTS.md`'s known-gaps list — TLS was
  the last one; multipart and WebSocket were already done in Phases 3/7). `src/tls/` +
  `include/nhttp/tls/`: `tls_context` (wraps `SSL_CTX*`, `create_server(cert_chain_file,
  private_key_file)`, `TLS1_2_VERSION` minimum) and `tls_stream` (implements `io::stream`,
  so it drops into `connection`/`listener` exactly like `socket_stream` — no changes needed to
  anything above the transport layer, confirming the seam decision #7 above was sound).
  - **Design: memory-BIO pair, not `SSL_set_fd`.** `tls_stream` gives OpenSSL two
    `BIO_s_mem()` BIOs (`SSL_set_bio`) instead of binding the SSL object directly to the socket
    fd. Every OpenSSL operation (`SSL_accept`/`SSL_read`/`SSL_write`) only ever touches these
    in-memory BIOs; `tls_stream` explicitly pumps ciphertext between them and the real
    `io::stream` (`co_await inner_->read/write(...)`) in `feed_rbio_from_network()` /
    `flush_wbio()`. This is what keeps OpenSSL entirely off the reactor thread's blocking path —
    `SSL_ERROR_WANT_READ`/`WANT_WRITE` become ordinary `co_await`s on the underlying stream
    instead of OpenSSL trying to `read()`/`write()` the fd itself, which would block a reactor
    thread outright (violates the one non-negotiable invariant in decision #3).
  - **`listener::listen_tls(ep, cert_chain_file, private_key_file)`** mirrors `listen()`'s
    per-worker `SO_REUSEPORT` loop exactly, sharing one `tls_context` (one `SSL_CTX*`) across
    all worker threads — safe per OpenSSL's own thread-safety guarantees for concurrent
    `SSL_new()` from a read-only-after-setup `SSL_CTX`. `dispatch()` and `run_connection()` were
    factored out of the old plain-HTTP-only `handle_connection()` so both the plain and TLS
    accept paths share one connection-handling implementation; only the transport
    construction (`socket_stream` vs `tls_stream` wrapping the same `socket_stream`) differs.
  - **A long, initially-misleading intermittent-failure investigation, resolved as NOT a library
    bug.** Manual `curl -k https://...` smoke-testing against a running `examples/nhttpd`
    instance during this phase intermittently failed (`SSL_ERROR_SYSCALL` client-side, ~50% of
    attempts, worse under `strace`), while `openssl s_client`, a dedicated blocking OpenSSL test
    client, and the automated Catch2 integration suite all passed reliably. Root-caused (not a
    plumbing/threading bug in `tls_stream`/`listener` at all) to **stale `nhttpd` processes left
    running from earlier manual test invocations in the same long-lived shell**, still bound via
    `SO_REUSEPORT` to an overlapping port from a previous run that was backgrounded (`&`) and
    never explicitly killed before starting a new instance on the same port. `SO_REUSEPORT`
    deliberately allows multiple independent listening sockets — even from unrelated,
    now-orphaned processes — to share one port, and the kernel load-balances *new* connections
    across all of them; a connection landing on the stale/half-shut-down instance fails, which
    looks exactly like an intermittent server-side bug from the client's perspective. Confirmed
    by finding such a leftover process alive via `ps aux` mid-investigation, and then by
    reproducing **zero failures in 100+ fresh TLS connections** (both via `curl` and the test
    client, sequential and concurrent bursts across all 8 default workers) once the environment
    was verified clean of leftover listeners. **Lesson for future manual smoke-testing in this
    repo (via WSL, across a long session): always confirm no previous `examples/nhttpd` (or any
    other test binary bound to a fixed, reused port) is still running before trusting a new
    manual test's results — `pgrep -f build/examples/nhttpd` before each manual run, or use port
    0 (OS-assigned) the way every Catch2 integration test already does, which is why the
    automated suite was never affected.**
  - Integration tests: `tests/integration/test_tls_server.cpp` (GET, POST body, keep-alive, and
    a many-fresh-connections-across-workers spread test) using `tests/support/
    raw_tls_http_client.hpp` — a small independent blocking OpenSSL client kept deliberately
    separate from `nhttp::tls`'s own code, so a shared bug couldn't hide behind both sides
    agreeing with each other. `NHTTP_ENABLE_TLS` (CMake option, default `ON`) gates the
    OpenSSL `find_package` requirement and all of the above; `NHTTP_HAVE_TLS` is the resulting
    compile definition guarding the `#ifdef`s in `listener.hpp`/`.cpp` and the example app.
    `examples/nhttpd` optionally also serves HTTPS on `port+1` when given cert/key path
    arguments (`./nhttpd [dir] [port] [cert.pem] [key.pem]`).
  - Full suite: 100/100 passing warning-free after this phase.
- **Phase 12 (Windows support & a hardened platform boundary) — done.** At the user's request to
  go beyond feature parity with the original (reverse proxy, Windows, HTTP/2 — QUIC explicitly
  deferred, see decision #8 above). Verified with real, compiled-and-tested support: natively on
  this machine via MSVC (VS 18 Community, `cl` 19.51, CMake/Ninja all present on Windows
  directly), not best-effort/unverified code — `ctest` is 100% green on both platforms after
  every phase below.
  - **The public platform headers already leaked POSIX types before this phase**, which is most
    of what "Windows support" turned out to mean: `include/nhttp/platform/address.hpp` embedded
    `in_addr`/`in6_addr`/`sockaddr*` in `ip_address`/`endpoint`'s own public interface;
    `socket.hpp` embedded `sockaddr_storage`/`socklen_t`/`ssize_t` and stored the native handle
    as `int` (wrong for a Windows `SOCKET`); `io_context.hpp` included `platform/epoll.hpp`
    purely to declare a private field. Fixed by making every public platform type portable:
    `ip_address` stores raw address bytes (`std::array<uint8_t,4/16>`) instead of
    `in_addr`/`in6_addr`; `socket_handle` uses a `native_socket_t` typedef (`#if defined(_WIN32)
    std::uintptr_t #else int #endif` — pure language, no OS header needed to write this) and
    `read`/`write` return `std::int64_t`; `accept()` returns `optional<pair<socket_handle,
    endpoint>>` instead of taking `sockaddr_storage&`; sockaddr conversion moved into
    `src/platform/{posix,win32}/sockaddr_convert.hpp` (internal-only, never installed);
    `io_context` now holds a `unique_ptr<platform::reactor>` (a new portable interface —
    `add`/`modify`/`remove`/`wait`, mirroring what `epoll_handle` already had) instead of a
    concrete `epoll_handle`, so `io_context.hpp` no longer names any OS type at all. `overlay`/
    `single_file` no longer include `<sys/stat.h>` — a new `platform::file_info`/`stat_file()`
    (portable `{kind, size, mtime}`) replaced `struct stat` in their public interfaces too.
  - **Design — kept the readiness-based `io_context` contract identical on both platforms**,
    deliberately not redesigning `async_socket`/`io_context` around IOCP's native completion
    model (a much larger, riskier rewrite touching every socket call site). The Windows
    `iocp_reactor` (`src/platform/win32/reactor.cpp`) uses **WSAEventSelect + a threadpool wait
    (`RegisterWaitForSingleObject`) bridging the Win32 event-object signal into the IOCP
    completion port via a plain `PostQueuedCompletionStatus`** — purely observational, so the
    *already-portable* retry-on-`would_block()` loops in `async_socket::read_some/write_some/
    accept/connect` and `socket_handle` needed zero changes. This single mechanism (one
    `WSAEventSelect` mask per registration: `FD_READ|FD_ACCEPT|FD_CLOSE` for read-interest,
    `FD_WRITE|FD_CONNECT|FD_CLOSE` for write-interest) uniformly covers listening sockets,
    connecting sockets, and established connections — see below for the two other designs tried
    and rejected first, and why.
  - **Three real bugs found and fixed while bringing this reactor up** (in order of discovery —
    left in detail since each is a genuine, non-obvious Windows socket-programming pitfall worth
    not re-deriving next time):
    1. *MSVC decodes this repo's UTF-8 source files using the system codepage, not UTF-8, absent
       a BOM.* On this (Korean-locale) machine that's CP949; every non-ASCII character in a
       comment (em dashes, arrows, etc. — this whole file included) tripped `C4819`, fatal under
       `/WX`. Fixed by adding `/utf-8` to the MSVC branch of `cmake/CompilerWarnings.cmake` —
       not cosmetic, this is required for the tree to compile on MSVC at all outside an
       English/UTF-8-default locale.
    2. *A zero-byte overlapped `WSARecv` — the standard trick for read-readiness on a connected
       socket — doesn't apply to a listening socket at all* (Winsock rejects it outright; a
       listening socket has no data channel to recv from), and the idiomatic IOCP answer
       (`AcceptEx`) *consumes* the pending connection as part of arming, which is incompatible
       with this reactor's "signal readiness, let the generic retry loop do the real op"
       contract — the same contract `read`/`write`/`connect` all rely on identically. This is
       what led to the unified `WSAEventSelect`-for-everything design above (superseding an
       initial version that mixed the zero-byte-`WSARecv` trick for reads with
       `WSAEventSelect`/`FD_ACCEPT` only for listening sockets specifically).
    3. *`WSAEventSelect(socket, NULL, 0)` must be called to undo a previous event association
       **before** closing the event handle* — closing the event first (skipping that call, my
       first attempt) leaves the *socket itself* in a broken state: every subsequent
       `accept()`/`recv()`/`send()` on it fails with `WSAENOTSOCK`, even though nothing closed
       the socket. Manifested as `async_socket accept/connect/read/write round trip` (a Phase-1
       unit test, the first one that actually drives the reactor for real I/O) crashing with
       "pure virtual method called" — traced via `gdb`'s coroutine-frame backtrace support to a
       `buffered_wire_stream` reading through a reference to an already-corrupted `io::stream`.
       Fixed in `iocp_reactor::remove()`'s listening-socket branch (`src/platform/win32/
       reactor.cpp`): `WSAEventSelect(fd, nullptr, 0)` before `WSACloseEvent`.
    4. *`SSL_set_tlsext_host_name`'s macro expansion contains an old-style C cast*, tripping
       `-Wold-style-cast` at every call site under GCC/Clang (only surfaced once Phase 13 added
       the first caller of it, client-mode TLS). Fixed by calling the underlying `SSL_ctrl`
       directly with a proper `static_cast`/`const_cast` instead of the macro — see
       `src/tls/stream.cpp`.
    5. *Windows has no `SO_REUSEPORT` equivalent for inbound TCP accept load-balancing* — binding
       N independent listening sockets to the same port doesn't give kernel-balanced accepts the
       way it does on Linux. `platform::socket_handle::set_reuse_port()` reports this honestly
       (returns `false` on Windows rather than silently pretending to succeed) instead of hiding
       it, and `listener::listen()`/`listen_tls()` treat that as a real capability check: when
       unavailable, they bind exactly **one** listening socket and round-robin each accepted
       connection onto a different worker explicitly via a new `io_context::schedule()` primitive
       (suspend the calling coroutine and resume it on a *specific* target context's own thread —
       the general form of the hand-off `thread_pool::run()` already did internally), rather than
       relying on the kernel. This is a genuine behavioral difference between platforms, not a
       cosmetic one, and is the reason `listener.hpp`'s doc comment now says "SO_REUSEPORT where
       available" rather than assuming it unconditionally.
  - A found-but-not-yet-fixed gap: no OpenSSL dev package (MSVC-linkable) is installed on this
    machine (only the MSYS2/Git-bundled `openssl.exe` CLI) — the Windows verification builds in
    this and later phases configure with `-DNHTTP_ENABLE_TLS=OFF`. `tls_context`/`tls_stream`
    themselves need no Windows-specific changes (OpenSSL is already cross-platform) — this is a
    build-environment gap on this specific machine, not a design gap.
  - CMake: `src/CMakeLists.txt` selects `platform/{posix,win32}/*.cpp` by `WIN32`, links
    `ws2_32`/`mswsock` instead of `Threads` there; top-level `CMakeLists.txt`'s hard "Linux only"
    warning now accepts `WIN32` too (still refuses anything else). `cmake/CompilerWarnings.cmake`
    gained an MSVC branch (`/W4 /permissive- /utf-8`, `/WX` under `NHTTP_WARNINGS_AS_ERRORS`) —
    no 1:1 equivalents exist for several GCC/Clang flags this project uses (`-Wshadow`,
    `-Wold-style-cast`, `-Wconversion`, ...), so parity is close but not exact.
  - Full suite: 96/96 passing warning-free on **both** Linux (WSL/ctest) and native Windows
    (MSVC/ctest) at the end of this phase; the example app was also manually verified live on
    Windows (dual-stack listen, static file serving via `overlay`, the router, graceful shutdown
    via `POST /exit`) — see `ReadMe.md`'s Windows build section for the exact commands.
- **Phase 13 (reverse proxy) — done.** A new extension, `reverse_proxy` (`src/server/extensions/
  reverse_proxy.cpp`), derived from `vpath` exactly like `router` is (same "mounted at a URL
  prefix, `on_handle()` instead of a nested registry" shape) — full scope: multiple upstreams
  with round-robin load balancing, HTTPS upstreams, and WebSocket upgrade passthrough, all four
  options the user picked when this was scoped, not just a single fixed upstream.
  - **Shared HTTP/1.1 message I/O extracted out of `connection.cpp`** into `src/server/
    http1_io.{hpp,cpp}` (`read_headers`, `make_body_stream`, `write_all`, `write_message_body`) —
    used unchanged by both the server-role `connection` and the new proxy client code, so the
    proxied upstream request/response framing can't silently diverge from the server's own (one
    chunked codec, one set of framing rules, exercised from both directions). `protocol::
    http_status` gained a `try_parse` mirroring `http_resource::try_parse`, needed to read an
    upstream's status line. `protocol::header_value_contains_token` was promoted from a
    `connection.cpp`-local helper to a shared one in `http_header.hpp` for the same reason.
  - **Client-mode TLS**: `tls_context::create_client(verify_peer)` and `tls_stream::connect
    (sni_hostname)` added alongside the existing server-only `create_server`/`accept()` — same
    memory-BIO pump loop, just `SSL_set_connect_state` + SNI (`SSL_ctrl`, see Phase 12's bug #4)
    instead of `SSL_set_accept_state`. Certificate verification defaults on.
  - **A real, lifetime-bug crash found and fixed while writing this**: `reverse_proxy::on_handle`
    built the upstream's decoded response body via `http1_io::make_body_stream`, which returns a
    `buffered_wire_stream` holding a *reference* to its caller's leftover-bytes buffer and wire —
    safe for `connection.cpp`'s own use (those references point into `connection`'s own
    long-lived member fields, alive for the whole connection) but not here: `on_handle`'s
    coroutine frame (and its local `upstream_leftover`/`wire` variables) is destroyed once
    `on_handle` returns, *before* `resp.body` is actually read from by the downstream
    connection's `write_response` — leaving a dangling reference, caught by Catch2's fatal-signal
    handler as "pure virtual method called" on the very first proxy test. Fixed by adding
    `http1_io::make_owned_body_stream` / `owned_buffered_wire_stream`, which *own* a moved-in
    copy of the leftover bytes and a `shared_ptr` to the wire instead of referencing the caller's
    stack frame — used by `reverse_proxy` in place of the reference-based version, with
    `make_body_stream`'s doc comment now explicitly warning future callers about this exact
    lifetime trap.
  - Round-robin uses a plain `std::atomic<std::size_t>` counter (same pattern as `listener`'s
    SO_REUSEPORT-fallback worker distribution in Phase 12). Known simplifications, in the same
    spirit as Phase 4's logged ones: **no upstream connection pooling** (a fresh connection per
    proxied request/upgrade) and **no active health checking** (a down upstream just fails that
    one request with a `502`) — both reasonable follow-ups, neither required for correct
    behavior today.
  - Integration tests (`tests/integration/test_reverse_proxy.cpp`): a plain GET and a POST body
    relayed end-to-end, a `502` for an unreachable upstream (using a raw `socket_handle` bound
    then immediately closed, so the port is *genuinely* refused rather than merely "bound but
    nothing ever `accept()`s from it" — the latter hangs the proxy's `connect()` instead of
    failing it fast, a mistake made and caught while writing this test), round-robin distribution
    across two fake upstreams, and a WebSocket echo round-tripped through the proxy. Full suite:
    105/105 passing warning-free on Linux and Windows.
- **Phase 14 (HTTP/2) — done, scope-reduced from the original plan.** New `src/http2/` module
  plus a new connection driver, `server::connection_h2`, sitting alongside (not replacing) the
  HTTP/1.1 `connection` — confirming Seam 1 from decision #8 above (`docs/protocol-
  extensibility.md`'s "a different connection type, same downstream machinery" claim) holds
  exactly as written: `connection_h2` decodes a multiplexed frame layer into many concurrent
  `request`/`response` exchanges, each dispatched through the *same* `listener::dispatch()` used
  by HTTP/1.1, with zero changes needed to `extension.hpp`, `router/`, or any extension.
  - **HPACK** (`src/http2/hpack.cpp`, RFC 7541): the static table (Appendix A), a from-scratch
    Huffman codec (the code table from Appendix B, transcribed from memory and — given the very
    real risk of a transcription error in a 257-entry bit-precise table — deliberately verified
    against **RFC 7541 Appendix C.4.1's own official Huffman-coded test vector** in
    `tests/unit/test_hpack.cpp` before trusting it for anything else; a decode-trie built once
    from the table, integer/string representation per §5.1/§5.2, and a dynamic table for the
    decoder side. The encoder always emits literal-without-indexing (never uses the dynamic
    table) — simpler, still fully RFC-compliant since a decoder must accept any valid
    representation choice, just not maximally compact; this also means `SETTINGS_HEADER_TABLE_
    SIZE` from the peer has no effect on this encoder's behavior (it never needs a table size
    limit if it never populates one), which `connection_h2`'s SETTINGS handling notes explicitly
    rather than silently ignoring for an unstated reason.
  - **Frame codec** (`src/http2/frame.cpp`): the 9-byte frame header, SETTINGS/WINDOW_UPDATE/
    RST_STREAM/GOAWAY/PING payload read/write, and padding-stripping shared by DATA and HEADERS.
    Server push, the legacy `Upgrade: h2c` bootstrap, and PRIORITY frame reordering are
    deliberately not implemented (push is deprecated in practice across current browsers;
    prior-knowledge — which *is* implemented — is what virtually every current client/tool,
    curl's `--http2-prior-knowledge` included, actually uses; PRIORITY is parsed just enough to
    skip its 5-byte payload and otherwise ignored, matching RFC 9218's own de-emphasis of the
    original priority scheme).
  - **`connection_h2`** owns the connection preface + SETTINGS exchange, per-stream state, and
    flow control (connection- and stream-level send windows, replenished via `WINDOW_UPDATE`
    immediately after consuming `DATA` — simple and correct, not the most bandwidth-efficient
    possible batching). A stream's response-writing coroutine suspends on a per-stream
    `coroutine_handle` when its send window is exhausted, resumed directly by the frame-read
    loop's `WINDOW_UPDATE` handling — deliberately not a generic `async::` primitive, since
    exactly one thing (this) needs it. Multiple concurrent streams are genuinely independent
    detached coroutines (spawned by the frame-read loop once a stream's headers — and body, if
    any — are fully received), serialized only where they must be: physically writing frames to
    the wire, via a small single-threaded cooperative `writer_lock` (not a real mutex — every
    coroutine for one connection runs on the *same* io_context thread by construction, so this
    only needs to arbitrate interleaved coroutine suspension, never true cross-thread
    contention). Verified interoperating with **real curl** (`--http2-prior-knowledge`,
    including `-Z` parallel mode genuinely multiplexing multiple streams over one TCP
    connection) against the example app's static files, router, and parameterized routes, before
    writing the automated suite.
  - **Negotiation, scope-reduced from the original plan: prior-knowledge cleartext only this
    round, ALPN-over-TLS explicitly deferred** (a real, honest scope cut made under time
    constraints, not an oversight — flagging it here the same way QUIC's deferral is flagged in
    decision #8, so it doesn't get silently assumed done later). `listener::handle_connection`
    (the plaintext accept path only — TLS's `handle_connection_tls` is untouched) peeks the
    first 4 bytes of *every* plaintext connection (`"PRI "` is the start of the HTTP/2 client
    preface, RFC 9113 §3.4, and is a request-line shape no real HTTP/1.1 method ever produces —
    chosen by the RFC specifically to be unambiguous this way) and constructs `connection_h2` or
    `connection` accordingly, replaying those bytes as each driver's initial buffer (`io::
    stream` has no non-destructive peek, so `connection`'s constructor gained an
    `initial_buffer` parameter to seed `read_buffer_` with them). This is a small, permanent
    4-byte read added to **every** plaintext connection now, not just HTTP/2 ones — confirmed
    the full existing suite (HTTP/1.1 included) still passes unchanged.
  - Other known simplifications: **request bodies are fully buffered** before a stream's handler
    runs (no live incremental request-body streaming over h2 — simpler, at the cost of holding a
    large upload fully in memory, capped at a fixed 16 MiB per stream for now, not yet wired to
    `params`); **response headers are assumed to fit in one HEADERS frame** (no CONTINUATION on
    the send side — real header sets are essentially always well under
    `SETTINGS_MAX_FRAME_SIZE`); **`SETTINGS_INITIAL_WINDOW_SIZE` changes only apply to streams
    opened afterward**, not retroactively adjusted on already-open streams per RFC 9113 §6.9.2's
    full generality (real clients send their initial SETTINGS before opening any streams in
    practice, making this a non-issue for typical usage); **`SETTINGS_MAX_CONCURRENT_STREAMS`
    is not enforced**; a `response::upgrade_handler` (e.g. from `websocket_endpoint` or
    `reverse_proxy`'s WS passthrough) gets a `501` over h2 instead of being silently attempted —
    RFC 9113 §8.5 doesn't support the HTTP/1.1 Upgrade mechanism at all, so there's no correct
    behavior to fall back to.
  - Integration tests (`tests/integration/test_http2_server.cpp`) use a `raw_h2_client` built on
    this library's *own* `http2::frame_header`/`hpack_encoder`/`hpack_decoder` — a deliberate
    departure from every other integration suite's independent-parser philosophy, justified
    because HPACK/framing correctness is already independently verified against RFC vectors in
    `test_hpack.cpp`, so reusing them here actually keeps the test focused on what it's meant to
    exercise (`connection_h2`'s driver logic — preface/SETTINGS handshake, multiplexing, flow
    control), not re-litigate codec correctness. One real test-client bug caught while writing
    this: the first version of the multiplexing test *discarded* frames belonging to a stream
    other than the one it was currently waiting on, silently losing that stream's response before
    a later call could read it — fixed by buffering (not discarding) out-of-target frames per
    stream in the test client itself. Covers: a single request/response, a POST body, and two
    concurrently-multiplexed streams read back in the *opposite* order they were requested
    (verifying real interleaving, not just sequential completion). Full suite: 113/113 passing
    warning-free on Linux and Windows.
- **Phase 15 (benchmarking against nginx/Apache) — done.** At the user's request to benchmark
  against nginx and Apache 2 and reflect the results in `ReadMe.md`. A sustained-load benchmark
  (something the existing test suite, dozens of requests per test, never exercised) found and led
  to fixing two real bugs, now on `main`:
  1. **`task<T>` leaked its own coroutine frame on every `co_await`, since this rewrite's first
     commit.** `operator co_await() &&` nulled the task's own `handle_` when handing the coroutine
     handle to a throwaway `awaiter` that never destroyed it — so nothing, ever, freed a completed
     task's frame. Since `task<T>` is the return type of nearly every async function here, this
     leaked on every nested `co_await` everywhere. Found via RSS growing 5.6&nbsp;MB → 5.78&nbsp;GB
     over 233K static-file requests; confirmed in isolation (2,000,000 awaited no-op tasks leaked
     ~125&nbsp;MB). **Fix**: stop nulling `handle_`, so the task object's own destructor frees the
     frame at the end of the `co_await` expression's temporary lifetime — the standard
     cppcoro-style pattern. Verified: 113/113 tests unchanged, RSS now flat under sustained load.
  2. **A client resetting a connection mid-response killed the entire process.** `write()` to an
     already-reset socket raises `SIGPIPE`, whose default disposition kills the whole process
     instantly with nothing for a debugger/sanitizer to catch — root-caused via `strace`, which
     showed `+++ killed by SIGPIPE +++` the instant `wrk` tore down its connection pool. **Fix**:
     `std::signal(SIGPIPE, SIG_IGN)` in `listener`'s constructor, POSIX-only (Windows reports
     `WSAECONNRESET` instead, no signal involved). Verified: 113/113 tests pass, and the benchmark
     that used to die within 30s now runs 426,113 requests without dying.
  - **Methodology**: nginx 1.24 and Apache 2.4.58 (event MPM, `MaxRequestWorkers` tuned 150→800)
    via `apt` on this machine's WSL Ubuntu, serving an identical 10&nbsp;KB static file;
    `wrk -t8 -c200 -d30s --latency`, both same-host loopback and a Docker Compose stack
    (`benchmark/docker/` — exercises real bridge-network kernel paths a loopback benchmark skips).
    Full numbers in `ReadMe.md`'s Benchmarks section.
  - **Result, honestly**: even after both fixes, nhttpd trailed nginx/Apache badly on this
    micro-benchmark (13.8K req/s vs. nginx's 139K, Apache's 38.6K) — a real, unoptimized
    architectural gap (every static-file request round-trips the blocking thread pool several
    times, where nginx uses one zero-copy `sendfile()` call), not a bug. See [PLAN.md](PLAN.md)
    (at the time) / Phase 16 below for closing it.
- **Phase 16 (executing PLAN.md — P1 through P5) — done.** At the user's request, every item in
  the performance plan was executed, including two (P3, and part of P4) whose own text said not to
  attempt them without profiling data — done anyway, with each outcome decided by A/B benchmarking.
  Full numbers are in `ReadMe.md`'s Benchmarks section; `PLAN.md` itself now only tracks what's
  still open (Windows `TransmitFile` for P1's sendfile path; real `perf` profiling on a host that
  can run it, since this WSL2 kernel can't). Combined result: loopback 13.8K → 58.0K req/s (~4.2×),
  Docker network-stack benchmark 11.2K → 35.9K req/s (~3.2×) — nhttpd now beats Apache in both.
  - **P1/P2**: a `sendfile(2)` fast path (`io::file_stream::native_fd()`, `platform::
    socket_handle::send_file()`/`supports_send_file()` — `false` on Windows, deferred rather than
    risking an unverifiable `TransmitFile`/IOCP integration) plus removing a redundant `stat()`
    between `overlay`/`single_file`'s `wants()` and `handle()`.
  - **P3**: a custom coroutine-frame allocator for `task<T>` — implemented, benchmarked, no
    measurable win over glibc's own `tcache`. Reverted; `task.hpp` is back to the Phase 15 fix only.
  - **P4**: `thread_pool`'s job queue, three iterations. A `std::counting_semaphore`-based
    lock-free queue measured **~2× slower** (every dequeue pays a sync cost even when the worker's
    already busy, unlike the mutex it replaced). A lock-free queue + mutex/condvar used only to
    wake idle workers fixed that, but only once `blocking_pool_size` stopped being
    over-provisioned: 64 threads contending over 8 cores lost more to context-switching than the
    lock-free queue saved, while the library's own default (4) hit **~69K req/s**, the best result
    of the phase — `blocking_pool_size` needs no manual tuning anymore, reversing this repo's prior
    advice. A further simplification (dropping an atomic wait-count gate around the wake
    notification, since glibc's `notify_one()` already skips its own syscall when nothing's
    waiting) measured equivalent and was kept for the simpler code. New:
    `include/nhttp/async/detail/mpmc_queue.hpp` (Vyukov's bounded MPMC design) with its own stress
    test; every iteration verified clean under ThreadSanitizer before being benchmarked.
  - **P5**: `platform::file_mapping` — a memory-mapped read path for `io::file_stream`, bounded to
    a sliding 4&nbsp;MiB window (never the whole file, to avoid exhausting address space on huge
    files) and opened *lazily* on a stream's first `read()` call. An eager version (opened in
    `open()`) was tried first and measurably regressed the common whole-file-GET case, which never
    calls `read()` at all once P1's sendfile path exists.
  - **Unrelated anomaly, noticed and corrected**: `.gitignore`, `LICENSE`, and
    `benchmark/benchmark-1.jmx` picked up a line-ending-only (LF→CRLF) diff as a side effect of a
    `git stash`/`stash pop` used to A/B-test P4 against the pre-P4 baseline. No content changed;
    reverted via `git checkout` before committing.
- **Phase 17 (PLAN.md's router item, `perf` becoming usable, and a new PHP-comparison benchmark)
  — done**, at the user's request to execute everything still open in `PLAN.md` except what it
  explicitly excludes.
  - **`perf` now works on this machine's WSL2 kernel** (`perf stat` and `perf record -g` for
    userspace symbols both verified working — kernel symbols still don't resolve, which is fine,
    nothing here profiles kernel code) — the blocker PLAN.md recorded in Phase 16 (no matching
    `linux-tools` package) no longer holds, whether from a WSL2 update or a package having been
    installed since. This unblocks real profiling instead of only A/B benchmarking for future
    work in this area; see the router item below for it actually being used this way.
  - **`router::route_match()`'s backtracking cost (the item PLAN.md flagged for exactly this)**:
    first, a dedicated benchmark harness was added — `benchmark/router/bench_router_main.cpp`, a
    minimal router with a static sibling (`users/admin`) competing with a param child (`:id`) at
    the same trie level, and two more nested param segments below it
    (`users/:id/posts/:postId/comments/:commentId`) — since no existing benchmark ever put the
    router under load (Phase 15/16 only ever exercised `overlay`'s static-file path). Profiling
    this harness with `perf record -g` under `wrk` load **confirmed PLAN.md's hypothesis with
    real data**: `malloc`/`cfree`/`_int_free`/`_int_malloc`/`operator new`/`operator delete` and
    several `std::_Rb_tree` construct/copy/erase symbols together accounted for roughly 10%+ of
    sampled CPU time, exactly the allocation churn PLAN.md's code-inspection predicted from
    `route_state trial = state;` copying a `std::map<std::string,std::string>` on every trie
    candidate tried (not just the winning path) and from `p->predicate_(std::string(segment))`
    allocating a string per parameter-child candidate purely to satisfy a signature that never
    needed ownership. Both were fixed exactly as PLAN.md proposed: `route_state::captures` is now
    a small hand-written `capture_map` (`include/nhttp/router/route.hpp`) — a linearly-scanned
    `std::vector<std::pair<std::string,std::string>>` behind the same `at()`/`count()`/`empty()`/
    `operator[]` interface `std::map` had, so no call site (examples, tests, the new benchmark)
    needed to change its usage, only its predicate lambda's parameter type — and every
    `param(...)` predicate across `facade`/`route`/`router`/`group_proxy` now takes
    `std::string_view` instead of `const std::string&`, so `route::route_match()` calls it
    directly on the segment with no allocation at all. `route::method_targets_`'s string-keyed
    lookup (PLAN.md's third, explicitly "likely lower-impact" bullet) was deliberately left
    alone — it's an O(1)-per-request cost, not paid per trie node visited like the other two, and
    `protocol::http_method` has no cheaper identity to key on today without a larger API change;
    still open if ever worth it.
    - **A/B methodology note, worth keeping for next time**: the first A/B attempt — alternating
      baseline/optimized runs of `wrk -t8 -c200` on this 8-thread (4-core) machine, with both the
      server and `wrk` itself contending for the same 8 logical CPUs — was too noisy to read at
      all (the "optimized" build measured *slower* in 3 of 4 interleaved trials). Pinning the
      server to CPUs 0-3 and `wrk` to CPUs 4-7 via `taskset` (and running 5 trials of each build
      back-to-back rather than interleaved) turned that into a small but consistent win: median
      97,085 req/s optimized vs. 93,998 req/s baseline (mean 96,922 vs. 91,169) on the deepest
      route (`users/:id/posts/:postId/comments/:commentId`, three captures). A modest result, but
      directionally consistent across every summary statistic once CPU contention noise was
      controlled for — combined with the allocation-churn evidence from `perf`, this was kept
      rather than reverted (unlike P3/P4's first attempts in Phase 16, which showed *no* direction
      or a *consistent regression* even after controlling for noise).
    - Full suite verified passing warning-free on **both** platforms after this change: 119/119 on
      Linux/WSL, 115/115 on native Windows/MSVC (test counts differ from Phase 16's 113/113 simply
      because the suite has grown since; not a regression).
  - **A new benchmark scenario, added at the user's request, comparing a genuinely dynamic
    endpoint (not a static file) against PHP and (added in a same-day follow-up) Node.js**:
    `benchmark/docker/nhttp/bench_counter_main.cpp` (an endpoint that reads an integer out of a
    file, increments it, writes it back, and responds with the new value — offloaded to
    `thread_pool`, serialized by a plain `std::mutex` since the whole server is one process)
    versus `benchmark/docker/php/counter.php` (an equivalent script, serialized by `flock()`) run
    under **nginx 1.27-alpine + PHP-FPM 8.3** (`benchmark/docker/nginx-php/`) and
    **`php:8.3-apache` (mpm_prefork + mod_php)** (`benchmark/docker/apache-php/`) — mod_php
    requires a non-threaded MPM, so prefork (not the event MPM the static-file benchmark's plain
    Apache uses) is the correct, standard real-world choice here, not a benchmark-only compromise
    — plus `benchmark/docker/node-counter/counter.js`, a plain unclustered Node.js 20 process
    (built-in `http`/`fs` only) doing the same read-increment-write with *synchronous*
    `fs.readFileSync`/`writeFileSync` calls specifically so the single event-loop thread blocks
    for the file I/O and serializes the critical section the same way the other three sides'
    mutex/`flock()` do — the async `fs.promises` API would have let two concurrent requests
    interleave between their read and their write and silently lose updates, the exact race the
    other three sides all pay to prevent. New `docker-compose.yml` services (`bench-nginx-php`,
    `bench-apache-php`, `bench-nhttp-counter`, `bench-node-counter`, `bench-client-scenario3`)
    gated behind a `scenario3` compose profile, per the user's explicit instruction that this
    benchmark only ever runs inside Docker — no loopback variant exists for it, unlike the other
    two benchmarks.
    - **Real numbers, current `main`** (4 CPUs/1&nbsp;GiB per container, 8t/200c/30s, same
      resource limits as the other Docker benchmark): nhttpd **57,351 req/s** vs. nginx+PHP-FPM's
      **3,182 req/s**, Apache+mod_php's **3,269 req/s**, and Node.js's **4,539 req/s** — roughly
      **12-18× faster** than any of the three, a much larger gap than the static-file scenarios,
      and an expected one: this mostly measures the cost of dispatching into an interpreted or
      single-threaded scripting layer per request (FastCGI round-trip, an in-process PHP
      interpreter invocation, or Node's one event-loop thread blocking on synchronous file I/O)
      against a compiled C++ handler — offloaded to a real thread pool — in the same process that
      already owns the connection, not nginx/Apache/Node's own general request-handling quality.
      Node came closest of the three (no per-request process/interpreter dispatch overhead the way
      PHP has) but is still fully serialized behind a single thread, unlike nhttp's multi-worker
      reactor. All four counters were read back after each run and cross-checked against expected
      request counts to confirm no lost updates under concurrency (every side's serialization held
      — Node included, once the synchronous-fs design choice above was made deliberately). Full
      results and reproduction steps are in `ReadMe.md`'s Benchmarks section.
    - **A packaging gotcha hit while building this**: `nginx:1.24-alpine` (Alpine 3.17) has no
      `php83`/`php83-fpm` package — Alpine only ships versioned PHP packages, and 3.17's repos top
      out at `php81`. Switched to `nginx:1.27-alpine` (a newer Alpine base) specifically so both
      PHP targets run the same PHP 8.3, rather than quietly comparing PHP 8.1 against 8.3.
    - **A real config bug found and fixed while bringing this up**: `php:8.3-apache`'s
      `mpm_prefork` config (unlike `mpm_event`, which the static-file Apache benchmark already
      tunes in `benchmark/docker/apache/mpm_event.conf`) needs `ServerLimit` raised alongside
      `MaxRequestWorkers` — Apache silently clamps `MaxRequestWorkers` down to the default
      `ServerLimit` of 256 otherwise (logged as a startup warning, easy to miss). Fixed in
      `benchmark/docker/apache-php/mpm_prefork_bench.conf`.
  - **Node.js added to the static-file benchmarks too, at the user's follow-up request** (both the
    Loopback and Docker network-stack scenarios, not just scenario 3): `benchmark/docker/node/
    static_server.js`, the same plain built-in-`http`/`fs`-only Node 20 server style as
    `counter.js`, serving the same deterministic 10&nbsp;KB `content/index.html` nginx/Apache/
    nhttpd already serve, via `fs.createReadStream(...).pipe(res)` per request (a real per-request
    read, page-cache-backed after the first hit, the same cost `sendfile(2)` pays on the other
    three — not a startup-time in-memory `Buffer` shortcut). `benchmark/docker/docker-compose.yml`
    gained a `bench-node` service (in the *default*, unprofiled service set, alongside
    `bench-nginx`/`bench-apache`/`bench-nhttp` — this benchmark was never scenario3-gated) and
    `client/run.sh` gained a `node` target. For the loopback scenario, Node 20 was installed via
    NodeSource on this machine's WSL2 Ubuntu (`apt`'s own `nodejs` there is only 18.19) and
    `static_server.js` was run directly against the same `/tmp/bench_site` nginx/Apache's existing
    loopback bench configs already point at (both were set up in an earlier session; still present
    and working on this machine).
    - **Real numbers, current `main`**: loopback — nginx 140,279 req/s, Apache 37,095 req/s,
      Node.js **4,188 req/s**, nhttpd 61,540 req/s; Docker network-stack — nginx 59,992 req/s,
      Apache 25,408 req/s, Node.js **3,412 req/s**, nhttpd 44,375 req/s. Node.js trails every
      C-based server by a wide margin in both — expected, and for a different reason than
      scenario 3's PHP comparison: a plain, unclustered `node` process is fundamentally
      single-threaded, so it can only ever use one CPU core regardless of connection count, unlike
      nginx's worker processes, Apache's threaded MPM, or nhttpd's own multi-worker reactor. A fair
      Node.js number at this concurrency would need the `cluster` module or a multi-process
      reverse-proxy in front of it — deliberately out of scope here, since the point was "how fast
      is one plain server process," which is also the honest comparison basis for every other
      entry in these two tables. Re-running nginx/Apache/nhttpd alongside Node.js this time (rather
      than reusing Phase 15/16's old numbers untouched) produced slightly different req/s for all
      three from natural run-to-run variance (e.g. loopback nginx 124,984 → 140,279) — not a
      regression in either direction, just normal noise on this machine; `ReadMe.md`/`ReadMe.ko.md`
      were updated with this run's full new numbers for internal consistency across the row, rather
      than only appending Node.js's row to stale figures. The Docker-scenario's separately-recorded
      RSS/memory-stability figures were **not** re-measured this round and are called out in
      `ReadMe.md`/`ReadMe.ko.md` as being from the original Phase 16 three-way run specifically, so
      they're not misread as tied to this run's request counts.
- **Phase 18 (finishing everything left on PLAN.md) — done**, at the user's explicit request to
  implement the rest of PLAN.md's open items after Phase 17. This closes out every item PLAN.md was
  tracking — nothing performance-related is open there anymore (see below).
  - **`route::method_targets_`'s string-keyed lookup** (the always-lower-priority third part of the
    router item, left open at the end of Phase 17): fixed the same way `route_state::captures` was —
    `protocol::http_method` gained a cheap `http_method_id` enum (`custom` plus one value per known
    method, computed alongside `flags_` in the constructor from the same `known_methods` table), and
    `route::method_targets_` is now a small flat `std::vector<method_target_entry>` (`{id, name,
    target}`) instead of `std::map<std::string, target_ptr>` — `get_target()`/`set_target()` compare
    `id` first (a plain integer, the fast path for all 9 known methods) and only fall back to
    comparing `name` when `id == custom`, which is what actually disambiguates two different custom/
    WebDAV-style methods from each other (the only case a bare id can't). No behavior change for any
    caller — `has_any_target()`/`get_target()`'s public contract is identical, only the internal
    storage changed. Verified: 119/119 on Linux, 115/115 on Windows, both warning-free.
  - **Real `perf`-based profiling revisit of P3/P4 (Phase 16's two open questions)**: profiled
    `benchmark/docker/nhttp/bench_main.cpp` (the static-file `sendfile(2)` path) the same way Phase
    17 profiled the router — `perf record -g` under `wrk` load. Findings, and why neither led to a
    code change:
    - **`thread_pool` overhead is small and profiles as small**: `worker_loop()` + `enqueue()` +
      `pthread_mutex_lock`/`unlock` together accounted for under 2% of sampled CPU time. This
      confirms P4's tuning (Phase 16) still holds — no further tuning opportunity is visible in a
      real profile, matching what the A/B-only evidence already suggested.
    - **No concentrated "coroutine-frame allocation" hotspot exists to fix**: total allocator
      overhead (`malloc`/`cfree`/`_int_malloc`/`_int_free`/`operator new`/`operator delete`) was a
      real ~8% of samples, but a caller-graph view showed it's diffuse — ordinary per-request
      `std::string`/`std::vector` allocations (the headers vector, `http_headers::set`, `stat_file`/
      `printf`-style formatting) funnel through the same `operator new`/`delete` symbols coroutine
      frames use, with nothing distinguishing frame allocation as a separate, larger cost among them.
      This is a real, data-backed explanation for P3's Phase-16 finding (a custom coroutine-frame
      allocator measured no win): the frame allocation cost isn't concentrated enough on its own to
      move the needle even if optimized in isolation, since it's the same size class as costs already
      spread across many unrelated call sites. No code change made; this closes out the question Phase
      16 could only answer by A/B.
  - **Windows: a real `TransmitFile`-based `sendfile(2)` equivalent** (PLAN.md's longest-standing
    open item, previously deferred pending exactly this design/verification work). The key blocker
    this item's own text called out — TransmitFile needing a genuine overlapped completion, which
    this reactor's plain reads/writes deliberately don't use — was confirmed for real (not assumed):
    Microsoft's own docs state `TransmitFile(..., lpOverlapped = NULL, ...)` **always runs fully
    synchronously regardless of the socket's non-blocking mode** ("the operation is executed as
    synchronous I/O... will not complete until the file has been sent") — so the hoped-for shortcut
    (call it like `write()`, treat `WSAEWOULDBLOCK` the same way) does not exist; a real overlapped
    completion is the only option that doesn't block a reactor thread, exactly as PLAN.md assumed.
    - **Design, kept deliberately narrow**: rather than redesigning `io_context`/`async_socket`
      around a native completion model (explicitly out of scope — PLAN.md's own "explicitly out of
      scope" list, and Phase 12's design note for why), this adds one small, additive, self-contained
      mechanism reusing the *existing* IOCP port `iocp_reactor` already owns:
      - `platform::reactor` gained one non-pure virtual, `native_completion_port()` (default
        `nullptr`; POSIX's `epoll_reactor` needs no override), and `io_context` exposes it verbatim.
        This is the only change to the general, portable reactor contract — everything else is
        Windows-only.
      - A new internal-only header, `src/platform/win32/overlapped_op.hpp` (same "never installed"
        status as `sockaddr_convert.hpp` in that directory), defines `overlapped_op : OVERLAPPED`
        carrying a `std::coroutine_handle<> waiter` plus a bytes/error result — the contract shared
        between whoever issues a real overlapped op (`async_socket::send_file`'s new Windows branch)
        and whoever drains completions (`iocp_reactor::wait()`).
      - `iocp_reactor::wait()`'s `GetQueuedCompletionStatus` loop gained exactly one new branch:
        a completion whose key is `0` is a genuine overlapped completion (TransmitFile), not one of
        the reactor's own synthetic readiness signals (which always key on a live `socket_state*`,
        never null) — cast `ov` to `overlapped_op*`, record its result, and resume its waiter.
        Resumption is deferred to a small local vector drained just before `wait()` returns (mirroring
        `io_context::process_ready_events()`'s already-established "collect during the loop, resume
        only after wait() returns" shape) rather than resuming inline mid-loop, so a resumed
        coroutine registering new interest or closing its socket can never observe `wait()`'s own loop
        state (`states_`, `produced`) half-updated.
      - `async_socket::send_file`'s Windows branch (`src/async/socket.cpp`) issues the actual
        `TransmitFile` call: associates the socket with the port via `CreateIoCompletionPort` —
        called unconditionally on every `send_file`, not tracked as "already associated" per-socket,
        since a second call on an already-associated handle just fails harmlessly and costs one cheap
        kernel call, which is simpler and can't go stale the way a per-socket cache keyed on a
        reused `SOCKET` value could — then calls `TransmitFile` through a process-cached function
        pointer (obtained once via `WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)`, `std::atomic`-
        guarded so the idempotent cache write itself isn't a data race). Both an immediate `TRUE`
        return and `FALSE`+`WSA_IO_PENDING` still suspend the coroutine and wait for
        `iocp_reactor::wait()` to deliver the completion (a handle associated with a port always gets
        one queued either way, unless `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` was set, which nothing
        here does) — only a genuine failure resumes the coroutine immediately without suspending.
      - `io::file_stream::native_fd()` — previously a hardcoded `-1` stub on Windows, since nothing
        called it there — now returns a real CRT fd via `_fileno()`; `async_socket::send_file`'s
        Windows branch converts that to a Win32 `HANDLE` itself via `_get_osfhandle()`, right where
        `TransmitFile` needs it, keeping `native_fd()`'s cross-platform contract (a CRT-style fd,
        not a raw OS handle) consistent with its POSIX meaning.
    - **Verified, not just compiled**: full suite green on both platforms (119/119 Linux, 115/115
      Windows, warning-free) — the existing static-file integration tests now exercise this path for
      the first time (`supports_send_file()` flipped `true`) and passed unchanged. Beyond that,
      manually smoke-tested exactly like Phase 9's/Phase 11's precedent for platform-specific work:
      a real `examples/nhttpd` instance on native Windows, serving a small text file and a 2&nbsp;MiB
      binary file (SHA-256 verified byte-exact through the new path), a byte-`Range` request (206,
      correct sub-range content — confirming the mmap/Range path Phase 16 built is untouched by this
      change), repeated keep-alive requests, and 50 concurrent full-file downloads with no crash and
      byte-exact content on a final re-check afterward.
    - **Real numbers**: no `wrk` build exists for native Windows, so this was measured with 16
      concurrent downloads of a 50&nbsp;MiB file (800&nbsp;MiB total) over loopback via parallel
      `curl` processes, A/B'd against a baseline build from before this phase (`git stash` to get a
      clean pre-TransmitFile tree, built to a separate binary, same machine, same test files,
      immediately before/after the real build) — baseline ~3.6-3.9s (~208-222&nbsp;MB/s) vs. this
      phase's build ~0.6-0.8s (~1.0-1.3&nbsp;GB/s) for the same 800&nbsp;MiB, a real **~5×**
      throughput improvement on this machine. Cruder than the Linux benchmark's `wrk`-based
      methodology (process-spawn overhead per `curl` invocation isn't free, and this measures
      raw large-file throughput rather than requests/sec on small files), but a clear, real,
      reproducible signal in the same direction sendfile(2)'s Linux numbers already showed.
    - **Known, deliberately accepted simplification**: no test exercises a file large enough to
      require more than one `TransmitFile` call (`nNumberOfBytesToWrite` is capped at
      `0x7FFFFFFE`, just under 2&nbsp;GiB) — `http1_io.cpp`'s existing send-loop already handles a
      short/partial completion by looping with the advanced offset (the exact mechanism a real
      multi-call transfer would use), so this is believed correct by construction rather than
      independently verified at that scale, which wasn't practical to set up here.
  - PLAN.md is now empty of open performance items — this phase closes out every one Phase 16 left
    behind and everything Phase 17 didn't already finish. Future performance work on this repo starts
    from a clean `PLAN.md` (still tracked separately from anything QUIC/HTTP-3-related or already
    listed as explicitly out of scope, both unchanged from before).
- Nothing left on the plan beyond QUIC/HTTP-3 (deferred, see decision #8), Phase 12's noted
  OpenSSL-on-Windows build-environment gap, and whatever `PLAN.md` currently tracks as open.
  Future work on this repo starts from here — see the module map and build instructions above,
  `ReadMe.md` for the user-facing API tour, and `PLAN.md` for the current performance to-do list.

## Working style notes for this repo specifically

- This is a large, multi-phase rewrite. Work proceeds phase by phase (see the plan file path
  above for the exact phase list); each phase should leave the tree buildable and fully tested
  before starting the next, and phases proceed without re-confirming with the user at each
  boundary — except deleting the legacy tree at the very end, which requires explicit
  confirmation first.
- When in doubt about whether some old behavior is worth preserving, check `CONCEPTS.md`
  (philosophy/invariants) and `USAGE.md` (call-site shapes) before assuming — both were written
  from a full read-through of the old implementation specifically to answer that question
  without re-reading old source every time.
