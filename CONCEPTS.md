# libnhttp — Design Concepts

**Language**: English | [한국어](CONCEPTS.ko.md)

This document captures the **design philosophy and invariants** of the original libnhttp
implementation, extracted for the purpose of a from-scratch rewrite. It intentionally does
**not** describe implementation details that are being discarded (thread-pool internals,
chunked-buffer allocator, etc.) except where the *idea* behind them must survive even if the
*code* does not.

## 1. Core philosophy

- **Event-driven, single-reactor-per-listener.** One `epoll` (or platform equivalent) loop
  drives all sockets registered to it. The reactor loop is the only thing allowed to touch a
  socket's readiness; everything else (parsing, handler execution) happens off to the side and
  reports back via a non-blocking poll, never by blocking the reactor thread itself.
- **Streaming first.** Request bodies and response bodies are `stream` objects, not buffered
  blobs. A response can be backed by a file, a byte range of a file, memory, or a
  programmatically generated source — the wire layer doesn't care, it only needs
  `read`/`get_length`/`is_nonblock`.
- **Layered request/response model.** There is a clean split between:
  - **raw layer**: bytes off the wire → `method + path + headers + body stream`, with zero
    opinions about routing, virtual hosts, or REST conventions.
  - **facade layer**: the raw layer wrapped into an ergonomic request/response object handed to
    user code — this is where extensions, vhosts, and routers plug in.
  This separation is what lets protocol upgrades (WebSocket) and future protocols slot in
  without the raw accept/parse loop knowing about them.
- **Everything pluggable is a chain, tried in priority order.** Listener-level extensions
  (vhost, static overlay, router, websocket handshake, user extensions) all implement the same
  two-phase contract — "do you want this request?" then "handle it" — and are tried in priority
  order, falling through to the next when declined. This is the single composition mechanism
  used for virtual hosting, path scoping, static file serving, and REST routing alike; none of
  them are special-cased into the listener itself.
- **Bounded resource usage over convenience.** Buffers are pooled/capped rather than growing
  unbounded per connection, and new connections are rejected outright when the pool is
  exhausted rather than degrading every connection's memory budget. A rewrite may choose a
  different mechanism (e.g. per-connection allocation with a global connection-count cap
  instead of a slab allocator) but should preserve the *guarantee*: total memory used by
  in-flight connections must be bounded by a configured limit, not by traffic.
- **Fluent, chainable configuration API.** Route/extension registration reads as a sentence:
  `router->get("path", target)->post(...)->group([...]){...}`. Whatever mechanism replaces
  `this_ptr<T>` should preserve this ergonomic surface without forcing every intermediate
  object into `shared_ptr` ownership if it doesn't need to be.
- **Tag-based extensible per-connection/per-request metadata.** Instead of every extension
  adding fields to a shared context object, each extension attaches its own strongly-typed "tag"
  (looked up by type) to the connection (`http_link`) or request. This is what lets vhost
  stacking, path scoping, and route-capture state coexist without a god-object. The lifetime
  distinction between **per-connection** tags (vhost/vpath scope stacks — survive across
  requests on a keep-alive connection) and **per-request** tags (route match state, middleware
  chain position — reset every request) is a load-bearing design choice, not an accident, and
  should be preserved explicitly in the new design (not collapsed into a single tag scope).

## 2. Concurrency model (subject to redesign, but the contract must be preserved)

The original model: a fixed thread pool executes blocking units of work (parsing, header
serialization, blocking body reads); the epoll thread never blocks — it polls "is this done
yet?" once per tick and re-enters the same state until a background result is ready. This is a
workaround for not having real async I/O primitives in C++17.

**What must be preserved, regardless of the concurrency primitive chosen for the rewrite
(thread-pool-with-polling vs. C++20 coroutines vs. something else):**
- The reactor thread must never block on a mutex/condvar/read waiting for a worker.
- CPU-bound or filesystem-blocking work (stat, file reads on non-nonblocking streams, string
  parsing of large bodies) must not stall other connections sharing the same reactor.
- A connection's protocol state machine must be resumable from wherever it left off after an
  async step completes — i.e. the state machine, not the call stack, owns "where am I."

A rewrite targeting C++20 should seriously consider replacing the blocking-task+poll idiom with
**stackless coroutines** (`co_await` on socket readiness / a future) since this removes an
entire class of the original code's complexity (state enums, "AGAIN vs RETRY vs SUCCESS"
step results) while preserving the same non-blocking guarantee. This is an architectural
decision to confirm before implementation (see accompanying plan).

## 3. Protocol layer conventions

- **Incremental parsing.** Every protocol element (request line, header line, chunk header,
  MIME type) is parsed by a function that can be fed a partial buffer and reports "need more
  bytes" distinctly from "malformed." This must survive because it's what makes the reactor
  model possible — parsing never assumes the whole request is already in memory.
- **Well-known constants without allocation.** Header names, methods, mime types, and status
  phrases that are known at compile time are compared without heap allocation or even
  `std::string` construction (`string_view`-shaped comparisons in the rewrite). Only genuinely
  dynamic values (a custom header name, a caller-supplied path) touch the allocator.
- **Method semantics as data, not switch statements.** Whether a method conventionally carries
  a request body, is idempotent, or is cacheable is looked up from a small flag table per
  method, not hard-coded per call site — new/custom methods can carry the same semantics.
- **A stream is a stream regardless of source.** File bodies, in-memory bodies, and byte-ranged
  sub-views of either are all just `stream` implementations. Range requests are a *decorator*
  over an inner stream, not a separate code path duplicated per content source. Conditional-GET
  (ETag / If-Modified-Since / If-Range) and Range handling are shared logic used by both
  "serve one fixed file" and "serve files under a directory" extensions — do not duplicate this
  logic between them in the rewrite.
- **Path normalization is centralized.** URL path qualification (resolving `.`/`..`, stripping
  redundant slashes) happens through one shared utility, used identically by the router, the
  static file overlay, and path-scoping extensions. A path-traversal bug fixed once must be
  fixed everywhere it's used — which is only possible if there's exactly one implementation.

## 4. Extension / composition model

- **Two-phase contract**: `wants(request) -> bool`, then `handle(request) -> response`,
  evaluated in ascending priority order across a registry, first acceptor wins, unhandled falls
  through to a default (404/501-shaped) response.
- **Composable scoping extensions.** Some extensions (vhost, path-prefix scoping) don't
  terminate the chain themselves — they narrow context (current host, remaining path) for a
  *nested* registry of extensions/routes they own, and clean up that narrowing when done
  (`enter`/`leave` around the nested dispatch). The router is *built on top of* the same
  path-scoping primitive used for plain virtual-path mounting — it is not a separate mechanism.
- **Everything the user registers is either terminal (produces a response) or a scope (narrows
  and delegates).** Keeping this distinction explicit (rather than one interface trying to do
  both) is what let the original router reuse vhost/vpath machinery instead of re-implementing
  prefix matching.

## 5. Routing (xfwk) semantics to preserve exactly

- **Trie over path segments**, not a flat pattern list — one node per path segment, four kinds:
  static (exact name), parameter (`:name`, filtered by an optional predicate), wildcard
  (catch-all remainder, cannot have further children), root (synthetic top).
- **Matching priority per segment, in order**: an exact static-name child is always tried
  before any parameter child; among multiple parameter children whose predicate accepts the
  segment, the one whose subtree match consumes the path *most deeply* wins (not simply the
  first predicate that passes); only if neither a static nor any parameter branch reaches a
  full match does the wildcard catch-all apply. **This exact tie-breaking rule was previously
  the subject of a real, shipped bug** (a naive "first successful param match wins" resulted in
  a shallower param match sometimes displacing a deeper, more specific one on backtrack) — the
  rewrite's test suite must include a regression test with sibling static/param/wildcard routes
  and multiple param candidates at the same segment to lock this behavior in.
- **Captured parameters must not leak across failed backtracking attempts.** If a parameter
  branch is tried and ultimately fails deeper in the tree, its capture must be rolled back
  before the next sibling is attempted.
- **Route registration is a fluent DSL** per HTTP method (`get/post/put/patch/delete/any`),
  with automatic promotion from "one target for this path" to "per-method dispatch table for
  this path" the first time a second method is registered on the same path — callers should
  never need to pre-declare "this path will have multiple methods."
- **Middleware composes around a target as a chain-of-responsibility**, each middleware
  deciding whether/how to call the next link, terminating at the target. Middleware can be
  attached to a single route or to a *group* of routes registered together (a "grouping" API
  that batches route-registration calls so a subsequent middleware attachment applies to all of
  them at once) — this grouping ergonomic is worth preserving as it is heavily used in the
  README/tests style of route registration.

## 6. Known gaps in the original implementation (do not silently "preserve" these — decide
   explicitly whether the rewrite fixes them)

- **Multipart form-data (`multipart/form-data`) parsing was never implemented** — only a
  data-holder shape existed, no actual parser. `nvalue.hpp`'s generic value tree exists
  almost solely for this unfinished feature and is otherwise dead weight.
- **WebSocket data-frame protocol was never implemented** — only the HTTP Upgrade handshake
  (101 Switching Protocols, `Sec-WebSocket-Accept` computation) worked; frame
  encode/decode/masking/fragmentation, `send`/`close`/`on_message` were stubs. The README
  advertises WebSocket support; the implementation did not deliver it.
- **No TLS/SSL support** — HTTP/1.1 plaintext only (explicitly called out as a future item in
  the original README).
- **A platform-macro naming/behavior trap**: the original headers redefined the `inline`
  keyword itself under MSVC (`#define inline __forceinline`) — a hazard worth deliberately
  avoiding in the rewrite rather than carrying forward.
- **Dead build-export machinery**: a DLL-export macro (`NHTTP_API`) existed but no shipped
  build configuration ever actually produced a shared library exercising the export path —
  everything was always statically linked in practice.

## 7. Naming note

The original HAL mutex wrapper was named "barrier" (`barrior_t`, sic) despite implementing
ordinary mutual exclusion, not a synchronization barrier. The rewrite should name primitives by
what they actually do (`mutex`, not `barrier`) to avoid this kind of misleading legacy name.
