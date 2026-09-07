# Performance improvement plan

**Language**: English | [한국어](PLAN.ko.md)

A live to-do list of performance follow-ups for this repo — **only items that are still open**.
Finished work is removed from here once done; its full story (what was tried, what was measured,
what got reverted and why) lives permanently in `CLAUDE.md`'s phase log, and the headline numbers
are in `ReadMe.md`'s Benchmarks section. Check those first for history — this file is only ever
"what's left."

## Open

- **Windows: a `sendfile(2)` equivalent for static-file responses.** The Linux fast path (see
  `CLAUDE.md`'s Phase 16 log) has no Windows counterpart yet — `platform::socket_handle::
  supports_send_file()` returns `false` there, so every response falls back to the generic
  read/write path. `TransmitFile` is the natural fit, but needs real overlapped-I/O completion
  handling this reactor's plain reads/writes don't currently use; do this once that's designed and
  verifiable, not as a quick add-on.
- **Real `perf`-based profiling, on a host that can actually run it.** This WSL2 kernel has no
  matching `linux-tools` package, and `strace -c` is too invasive to stand in for it (see
  `CLAUDE.md`'s Phase 16 log). A couple of questions from that phase were settled by direct A/B
  benchmarking instead, which is valid evidence but coarser than a real profile — worth revisiting
  with `perf` on a native Linux host if further micro-optimization in this area is ever wanted:
  coroutine-frame allocation churn (P3 in the phase log, reverted for lack of a measured win) and
  finer-grained `thread_pool` tuning beyond "don't over-provision workers" (P4 in the same log).
- **`router::route_match()`'s per-candidate backtracking cost.** Everything benchmarked so far
  (Phase 16) is `overlay`'s static-file path — the `router` module has never been under load in
  any of these numbers, and code inspection already shows concrete, plausible overhead worth
  measuring properly before touching:
  - `route_state` (`include/nhttp/router/route.hpp`) carries its captures in a
    `std::map<std::string, std::string>` — a red-black tree, so `route_state trial = state;`
    (taken once per static-child attempt *and* once per candidate in the parameter-children loop,
    for every trie node visited, not just once per final match — see `route::route_match()`) does
    a fresh set of node allocations every time, most of which are for branches that end up
    discarded when backtracking. A route with even one or two path parameters plausibly pays this
    on every segment of the search, not just the winning path.
  - `route::route_match()` calls `p->predicate_(std::string(segment))` — an allocating
    `std::string` construction per parameter-child candidate per segment, purely to satisfy a
    predicate signature (`std::function<bool(const std::string&)>`) that never actually needs
    ownership (every real predicate in `USAGE.md`/the example app just compares equality).
  - Likely lower-impact, worth checking anyway while in this code: `route::method_targets_` is a
    `std::map<std::string, target_ptr>` keyed by method *name string*, looked up once per matched
    request via `get_target()` — cheaper than the above since it's O(1) per request instead of
    O(nodes visited), but still a string-comparing tree lookup where `protocol::http_method`
    already has a cheaper identity to key on.
  - **Before changing anything**: this repo's benchmark suite (`benchmark/docker/`,
    `bench_server.cpp`-style harnesses) only exercises `overlay`, so there's no existing measured
    baseline for routed requests at all. Extend the benchmark methodology to a router-backed
    endpoint (e.g., a route with one or two path parameters, matching this codebase's own
    `:user/profile`-style examples) *first*, establish a baseline, then apply candidate fixes
    (captures as a small `std::vector<std::pair<...>>` instead of `std::map`; predicates taking
    `std::string_view`) one at a time and A/B against it — exactly the methodology Phase 16's P3
    and P4 already validated as necessary (both looked like clear wins on paper; one measured as a
    real win, two didn't and were reverted).

## Explicitly out of scope

- Anything already tracked separately: QUIC/HTTP-3 (see `CLAUDE.md`'s architecture decisions,
  point 8), ALPN-negotiated HTTP/2 over TLS, HTTP/2 `CONTINUATION`/server push (see `CLAUDE.md`'s
  Phase 14 log).
- Reverse-proxy connection pooling and active upstream health checks (`CLAUDE.md`'s Phase 13 log
  already lists these as known, reasonable follow-ups outside this round's scope).
- Rewriting `io_context`/`async_socket` around a native completion model instead of the current
  portable readiness-based contract (`CLAUDE.md`'s Phase 12 design note explains why that was
  deliberately avoided even for Windows/IOCP) — nothing here requires revisiting that.
