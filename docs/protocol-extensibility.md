# Protocol Extensibility — HTTP/2 & QUIC Readiness Review

**Language**: English | [한국어](protocol-extensibility.ko.md)

This document records a deliberate review of Phases 4–7 (server core, extensions, router,
WebSocket) against the three architectural seams the redesign committed to up front (see
`CONCEPTS.md` and `CLAUDE.md`'s architecture decisions): the codebase should be able to grow
HTTP/2 and eventually QUIC support later **without a redesign**, even though neither is
implemented now. Nothing in this document is implemented — it's the audit, plus the one small
interface fix the audit turned up.

## Seam 1 — connection vs. exchange split

**Claim to verify:** routing, extensions, and handlers must never assume "one request per
connection," so a future multiplexing HTTP/2 driver can run many concurrent exchanges over a
single connection.

**Findings:** holds. `request`/`response` (`include/nhttp/server/request.hpp`,
`response.hpp`) carry no reference back to the `connection` that produced them — a handler,
`extension`, or `router` only ever sees a `request&`, never the connection object. The
connection-scoped vs. request-scoped tag split (`connection::tags()` vs. `request::tags`,
`CONCEPTS.md` §1) is exactly what keeps this honest: anything that legitimately needs to persist
*across* requests on one connection (a vhost stack, in the original design) has an explicit home
for that, and everything else resets per exchange. `extension_registry::dispatch`,
`router::on_handle`, and `middleware_stack::handle` all operate purely on `request&` and return
a `response` (or `task<response>`) — none of them reach into connection internals, and nothing
in their signatures encodes "there is exactly one of these per socket."

The one place a *connection* legitimately embeds protocol-specific sequencing is
`server::connection` itself (`src/server/connection.cpp`): its `run()` loop is, correctly, an
HTTP/1.1-specific driver (read one request, dispatch, write one response, repeat until
keep-alive ends). That's expected — a future `http2_connection` would be a *different*
connection type that decodes a multiplexed frame layer into many concurrent `request`/`response`
pairs and dispatches each through the *same* `extension_registry`/`router`/`handler_type`
machinery used today. No changes to `extension.hpp`, `router/`, or the extensions in
`server/extensions/` would be needed to add such a driver.

## Seam 2 — transport-agnostic async stream

**Claim to verify:** a driver must talk to the network only through the `io::stream`
abstraction, never assume a raw TCP socket, so a future QUIC transport (fundamentally different:
UDP-based, no single ordered byte stream) can implement the same interface underneath without
touching anything above it.

**Findings: one real gap, fixed during this review.** `server::connection` originally held its
wire as a concrete `io::socket_stream` value member and took one by value in its constructor —
correct in spirit (all reads/writes went through `io::stream`'s virtual interface), but the
*type* was needlessly coupled to TCP sockets specifically. A hypothetical QUIC stream wrapper
implementing `io::stream` could never have been substituted without changing `connection`'s own
signature. Fixed in this phase: `connection` now takes and stores `std::shared_ptr<io::stream>`
(`include/nhttp/server/connection.hpp`), and `listener::handle_connection`
(`src/server/listener.cpp`) is the only place that still knows the wire happens to be a
`socket_stream` — it constructs one and hands it up as a plain `io::stream`. The WebSocket
upgrade handoff (`response::upgrade_handler`, `connection::write_response`) already passed the
wire onward as `shared_ptr<io::stream>`, so this fix also removed a redundant re-wrap that used
to happen at every upgrade.

Everywhere else already honored this seam without changes needed: `io::range_stream`,
`protocol::chunked_decoder_stream`, `protocol::multipart_reader`, and `ws::ws_connection` all
operate on `shared_ptr<stream>`/`stream&`, never on a concrete socket type.

## Seam 3 — wire-decoded header model

**Claim to verify:** the router and extensions must see headers as decoded key/value pairs,
never raw bytes, so HPACK/QPACK-decoded HTTP/2/3 headers are indistinguishable from HTTP/1.1
text headers at that layer.

**Findings:** holds, cleanly. `protocol::http_headers` (`include/nhttp/protocol/http_header.hpp`)
is a plain ordered collection of `{name, value}` string pairs with case-insensitive lookup —
nothing about it encodes CRLF-terminated text lines or any other wire detail. `http_header::
try_parse` is the *only* thing that knows about HTTP/1.1's colon-and-CRLF wire format, and it's
called exclusively from `connection::read_headers`. Every extension, `router`, and every
`static_content.hpp` conditional-GET/Range check reads headers via `req.headers.get(...)` /
`.isset(...)` — a hypothetical HTTP/2 driver decoding HPACK into the same `http_headers`
structure (mapping `:method`/`:path` pseudo-headers onto `http_resource`, everything else
straight across) would need zero changes to `router/`, `server/extensions/`, or
`static_content.cpp` to work correctly.

## Summary

| Seam | Status | Action taken |
|---|---|---|
| Connection vs. exchange split | Holds | none needed |
| Transport-agnostic stream | Gap found | `connection` now takes `shared_ptr<io::stream>`, not a concrete `socket_stream` |
| Wire-decoded header model | Holds | none needed |

No HTTP/2 or QUIC code is written as a result of this review — per the plan, this phase is
audit-only plus whatever small interface fix the audit found. The one fix above (Phase 8) is a
non-behavioral refactor: it changes what *type* `connection` accepts, not what it does with it,
and every existing test in the suite (96/96 at the time of this review) passes unchanged.
