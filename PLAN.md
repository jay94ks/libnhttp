# Performance improvement plan

**Language**: English | [한국어](PLAN.ko.md)

A live to-do list of performance follow-ups for this repo — **only items that are still open**.
Finished work is removed from here once done; its full story (what was tried, what was measured,
what got reverted and why) lives permanently in `CLAUDE.md`'s phase log, and the headline numbers
are in `ReadMe.md`'s Benchmarks section. Check those first for history — this file is only ever
"what's left."

## Open

Nothing right now — Phase 18 (see `CLAUDE.md`'s phase log) closed out every item this file was
tracking: the Windows `TransmitFile`-based `sendfile(2)` equivalent, the real `perf`-based revisit
of P3/P4, and `route::method_targets_`'s string-keyed lookup. Check `CLAUDE.md`'s phase log for the
full story of each and `ReadMe.md`'s Benchmarks section for headline numbers before starting new
performance work here.

## Explicitly out of scope

- Anything already tracked separately: QUIC/HTTP-3 (see `CLAUDE.md`'s architecture decisions,
  point 8), ALPN-negotiated HTTP/2 over TLS, HTTP/2 `CONTINUATION`/server push (see `CLAUDE.md`'s
  Phase 14 log).
- Reverse-proxy connection pooling and active upstream health checks (`CLAUDE.md`'s Phase 13 log
  already lists these as known, reasonable follow-ups outside this round's scope).
- Rewriting `io_context`/`async_socket` around a native completion model instead of the current
  portable readiness-based contract (`CLAUDE.md`'s Phase 12 design note explains why that was
  deliberately avoided even for Windows/IOCP) — nothing here requires revisiting that.
