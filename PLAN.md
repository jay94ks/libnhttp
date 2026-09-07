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
- **Real `perf`-based profiling, now that a host to run it on exists.** Phase 16's blocker (no
  matching `linux-tools` package for this WSL2 kernel) no longer holds — `perf stat` and
  `perf record -g` both work here now (userspace symbols resolve fine; kernel symbols still
  don't, which doesn't matter for profiling this codebase's own code). Phase 17 used it for the
  router item below, but the two specific questions Phase 16 could only settle by A/B benchmarking
  are still open to revisit with a real profile: coroutine-frame allocation churn (P3, reverted
  for lack of a measured win) and finer-grained `thread_pool` tuning beyond "don't over-provision
  workers" (P4) — both in `CLAUDE.md`'s Phase 16 log.
- **`route::method_targets_`'s string-keyed lookup.** The two more impactful parts of this same
  item (the `route_state` capture map and the per-candidate predicate allocation) were fixed in
  Phase 17 (see `CLAUDE.md`'s phase log) after `perf`-profiling `benchmark/router/
  bench_router_main.cpp` confirmed both as real hotspots. This third, always-lower-priority part
  is still open: `route::method_targets_` is a `std::map<std::string, target_ptr>` keyed by method
  *name string*, looked up once per matched request via `get_target()` — cheaper than the other
  two since it's O(1) per request instead of paid per trie node visited during backtracking, but
  still a string-comparing tree lookup where `protocol::http_method` has no cheaper identity to
  key on today. Worth a small enum/id addition to `protocol::http_method` if this is ever measured
  as worth it — `bench_router_main.cpp` now exists as the baseline to A/B it against.

## Explicitly out of scope

- Anything already tracked separately: QUIC/HTTP-3 (see `CLAUDE.md`'s architecture decisions,
  point 8), ALPN-negotiated HTTP/2 over TLS, HTTP/2 `CONTINUATION`/server push (see `CLAUDE.md`'s
  Phase 14 log).
- Reverse-proxy connection pooling and active upstream health checks (`CLAUDE.md`'s Phase 13 log
  already lists these as known, reasonable follow-ups outside this round's scope).
- Rewriting `io_context`/`async_socket` around a native completion model instead of the current
  portable readiness-based contract (`CLAUDE.md`'s Phase 12 design note explains why that was
  deliberately avoided even for Windows/IOCP) — nothing here requires revisiting that.
