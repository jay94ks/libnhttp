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

The full redesign plan (architecture + phased build order) lives at
`C:\Users\jay94\.claude\plans\dazzling-fluttering-badger.md` on the machine this was planned on
— it won't be present in a fresh clone, so the summary below is the durable record.

## Architecture decisions (confirmed with the user — do not silently relitigate these)

1. **C++20**, not 17. Coroutines are used for the entire async I/O model.
2. **CMake** build system, replacing the old `Makefile`/`.vcxproj`/`.sln`. **Linux-first**:
   Windows support is deliberately deferred, but the platform boundary (`src/platform/`) must
   stay a clean seam so Windows can be added later without redesigning anything above it.
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
7. **TLS/SSL is out of scope for this round**, but the stream/transport abstraction must stay
   swappable so TLS can be added later without a redesign.
8. **HTTP/2 and QUIC are not implemented**, but three architectural seams must be preserved
   so they can be added later without a redesign: (a) a connection/exchange split — router and
   handler code must never assume one request per connection; (b) a transport-agnostic async
   stream abstraction — drivers must talk to the network only through it, never assume a raw
   TCP socket; (c) headers reach the router/extensions as decoded key/value pairs, never as
   raw wire bytes, so HPACK/QPACK-decoded headers are indistinguishable from HTTP/1.1 ones at
   that layer.
9. **Warning-free build** (`-Wall -Wextra -Wpedantic`, treated as errors) is a hard requirement,
   not aspirational — don't add code that needs warnings suppressed to compile.

## Directory layout

```
CMakeLists.txt              top-level build config
cmake/                      CMake helper modules (compiler warnings, etc.)
include/nhttp/              public headers (mirrors src/ module layout)
src/
  platform/                 epoll wrapper, raw socket wrapper, ipv4/ipv6 endpoint types
  async/                    task<T>, io_context, io_context_pool, socket awaitables, timers,
                             thread_pool (blocking-work offload)
  io/                       async stream interface + memory/file/range/socket streams
  protocol/                 header/method/status/mime/date/query_string/resource/urlencode,
                             chunked codec, multipart/form-data parser
  server/                   listener, HTTP/1.1 connection coroutine, request/response facade,
                             tag storage, params/config
  server/extensions/        extension registry, vhost, vpath, static overlay, single-file serving
  router/                   trie-based router (facade/route/middleware/target), successor to xfwk
  ws/                       WebSocket handshake + RFC6455 frame codec + async send/recv
  depends/                  vendored third-party (sha1, utf8) — reused as-is unless trivially
                             replaceable by a standard facility
tests/
  unit/                     one file per module, Catch2
  integration/              real listener on loopback, driven by a small test HTTP/WS client
examples/
  nhttpd/                   sample app mirroring the old demo endpoints (written in the final phase)
libnhttp/, libnhttp-tests/, nhttpd/, coverage/, Makefile, *.vcxproj, *.sln
                             LEGACY — old C++17 implementation, untouched, reference-only,
                             deleted only after new implementation reaches parity + user confirms
```

## Build & test (Linux — this repo is developed via WSL Ubuntu when the host is Windows)

This machine's WSL Ubuntu instance has GCC 13.3.0, CMake 3.28, and Ninja pre-installed and is
the environment used to actually compile/run/test this project, since the reactor is
epoll-based and cannot build or run on native Windows. If invoking from a Windows shell, prefix
commands with `wsl.exe -d Ubuntu -- bash -lc "..."`; paths under `C:\GitHub\libnhttp` are
reachable inside WSL at `/mnt/c/GitHub/libnhttp`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Every phase of work must leave the tree in a state where the above three commands succeed with
zero compiler warnings before moving on.

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
- Nothing left on the plan. Future work on this repo starts from a clean, fully-tested C++20
  implementation — see the module map and build instructions above, and `ReadMe.md` for the
  user-facing API tour.

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
