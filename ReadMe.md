# libnhttp

**Language**: English | [한국어](ReadMe.ko.md)

<p align="center">
<img src="https://raw.githack.com/jay94ks/libnhttp/main/logo.png" />
</p>

An event-driven, coroutine-based HTTP/1.1 **and HTTP/2** server library for C++20, built on a
multi-threaded reactor — epoll on Linux, IOCP on Windows (both natively supported).

This is a from-scratch redesign of the original libnhttp: same feature set and design
philosophy, none of the old implementation carried forward. The design rationale, what changed
and why, and the historical bugs some of this code deliberately guards against are documented
in [CONCEPTS.md](CONCEPTS.md), [USAGE.md](USAGE.md), and
[docs/protocol-extensibility.md](docs/protocol-extensibility.md).

**Table of contents**

* [License](#license)
* [Requirements](#requirements)
* [Building](#building)
* [Quickstart example](#quickstart-example)
* [Architecture overview](#architecture-overview)
* [Static file serving](#static-file-serving)
* [Virtual hosting](#virtual-hosting)
* [Routing (the `router` module)](#routing-the-router-module)
* [WebSocket](#websocket)
* [TLS/SSL](#tlsssl)
* [Reverse proxy](#reverse-proxy)
* [HTTP/2](#http2)
* [Windows](#windows)
* [Design documents](#design-documents)

## License

```
MIT License

Copyright (c) 2021 Jay (jay94ks@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

No third-party code is vendored in this rewrite — the WebSocket handshake's SHA-1/base64 are
self-contained implementations under the same license (see `src/ws/sha1.cpp`).

## Requirements

* Linux (epoll-based reactor) or Windows (IOCP-based reactor)
* GCC ≥ 11, Clang ≥ 14, or MSVC ≥ 19.29 (VS 2019 16.10) — C++20 coroutines
* CMake ≥ 3.20
* OpenSSL (for TLS/SSL support; see `NHTTP_ENABLE_TLS` below to disable) — on Windows this means
  an MSVC-linkable OpenSSL dev package (e.g. via vcpkg), not just the `openssl` CLI

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Build options (`-D<option>=ON|OFF` at configure time):

| Option | Default | Meaning |
|---|---|---|
| `NHTTP_BUILD_TESTS` | `ON` | Build the Catch2 test suite |
| `NHTTP_BUILD_EXAMPLES` | `ON` | Build `examples/nhttpd` |
| `NHTTP_WARNINGS_AS_ERRORS` | `ON` | Treat compiler warnings as errors |
| `NHTTP_ENABLE_TLS` | `ON` | Build TLS/SSL support (requires OpenSSL) |

The library builds warning-free under `-Wall -Wextra -Wpedantic` (plus several more, see
`cmake/CompilerWarnings.cmake`) — this is a hard requirement, not aspirational. On MSVC the
closest equivalent is used instead (`/W4 /permissive-`, no 1:1 match for every GCC/Clang flag).

On Windows, from a native shell with the MSVC toolchain on `PATH` (e.g. after running
`vcvars64.bat`), the same three commands work unchanged:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Quickstart example

```cpp
#include "nhttp/server/listener.hpp"
#include "nhttp/server/extensions/overlay.hpp"
#include "nhttp/router/router.hpp"

using namespace nhttp::server;
using namespace nhttp::router;
using namespace nhttp::platform;

int main() {
	listener srv;

	// serve static files from "." (falls back to index.html for directories).
	srv.extends(overlay_of(".", "index.html", srv.blocking_pool()));

	// a small REST API.
	auto api = make_router();

	api->get("whoami", target_by([](request&) {
		return make_response("I'm jay.");
	}));

	api->get("/:user", target_by([](request& req) {
		return make_response(route_of(req).captures.at(":user") + " is ...");
	}));

	srv.extends(api);

	if (!srv.listen(endpoint(ip_address::loopback_v4(), 8080)))
		return 1;

	srv.run(); // blocks; call srv.stop() (e.g. from a handler) to return
	return 0;
}
```

See `examples/nhttpd/main.cpp` for a fuller example (static files + a REST API with route
groups, parameter predicates, and a WebSocket echo endpoint), and [USAGE.md](USAGE.md) for more
call-site patterns.

## Architecture overview

```
src/platform/    portable address/socket/reactor/file_info API, backed by posix/ (epoll) or
                 win32/ (IOCP) — never both in one build
src/async/       task<T> (coroutines), io_context (the reactor), io_context_pool
                 (SO_REUSEPORT multi-threading, or an explicit round-robin fallback where
                 SO_REUSEPORT isn't available), thread_pool (blocking-work offload)
src/io/          async stream interface + memory/file/range/socket streams
src/protocol/    header/method/status/mime/date/query-string/resource parsing,
                 chunked transfer codec, multipart/form-data streaming parser
src/server/      listener, HTTP/1.1 connection and HTTP/2 connection_h2 (one coroutine each,
                 no state-machine enum), request/response, the extension registry,
                 vhost/vpath/overlay/single_file/reverse_proxy
src/router/      the REST router: path trie, fluent registration DSL, middleware, grouping
src/ws/          WebSocket handshake + real RFC 6455 frame I/O
src/http2/       HPACK (RFC 7541) + frame codec (RFC 9113)
```

A `listener` runs one `io_context` per worker thread. On platforms with `SO_REUSEPORT` (Linux),
each worker binds its own socket per listening endpoint and the kernel load-balances accepted
connections across threads; where that's unavailable (Windows has no equivalent), one worker
accepts and explicitly round-robins each new connection onto another. Either way, a connection
stays on whichever worker ends up owning it for its entire lifetime.

## Static file serving

```cpp
srv.extends(overlay_of("/var/www", "index.html", srv.blocking_pool()));
```

Conditional GET (`ETag`/`If-None-Match`/`If-Modified-Since`) and byte-`Range` requests
(`206 Partial Content`) work automatically, with no handler code — the same engine backs both
directory serving (`overlay`) and single-fixed-file serving (`single_file`).

> **Note:** a `router` (or any extension built on `vpath`) mounted at `"/"` accepts every
> request in its cheap `wants()` check, so give a static overlay (or anything else that should
> get first refusal) a **lower** priority number than the router — see the priority argument on
> `overlay`'s constructor and `examples/nhttpd/main.cpp` for a worked example. This is covered
> in more depth in `CLAUDE.md`'s architecture-decisions log.

## Virtual hosting

```cpp
auto example_com = vhost_for("www.example.com"); // exact hostname, a regex, or a predicate
example_com->extends(overlay_of("./example.com", "index.html", srv.blocking_pool()));
srv.extends(example_com);
```

## Routing (the `router` module)

```cpp
auto router = make_router();

router->get("whoami", target_by([](request&) { return make_response("I'm jay."); }));

router->group([](facade_ptr inner) {
	inner->get(":user/profile", target_by([](request& req) {
		return make_response(route_of(req).captures.at(":user") + " is ...");
	}));

	inner->post(":user/set", target_by([](request& req) -> async::task<response> {
		std::string body;
		co_await req.body->read_all(body);
		co_return make_response(std::move(body));
	}));

	inner->param(":user", [](const std::string& name) {
		return name == "jay" || name == "kay";
	});
})->prepend(std::make_shared<my_logging_middleware>());

srv.extends(router);
```

Matching priority per path segment is **static child → deepest-matching parameter child →
wildcard** — this exact tie-break was the subject of a real, previously-shipped bug, and is
locked in by a dedicated regression test (`tests/unit/test_router_route.cpp`).

## WebSocket

```cpp
srv.extends(websocket_endpoint_for("/ws", [](std::shared_ptr<ws::ws_connection> ws) -> async::task<void> {
	for (;;) {
		auto msg = co_await ws->receive();
		if (!msg) co_return;
		co_await ws->send_text(msg->data); // echo
	}
}));
```

Full RFC 6455 frame I/O (masking, fragmentation, ping/pong, close handshake) — not just the
handshake.

## TLS/SSL

```cpp
srv.listen_tls(endpoint(ip_address::loopback_v4(), 8443), "cert.pem", "key.pem");
```

Requires OpenSSL and `NHTTP_ENABLE_TLS` (on by default). `listen_tls` mirrors `listen`'s
per-worker `SO_REUSEPORT` binding, so a TLS endpoint gets the same multi-threaded load
distribution as plain HTTP. Internally, `tls_stream` implements the same `io::stream` interface
as `socket_stream` — it drives OpenSSL against a pair of in-memory BIOs and pumps ciphertext to
and from the connection's own async stream, so the TLS handshake and every subsequent read/write
are ordinary `co_await`s and never block a reactor thread. Everything above the transport layer
(routing, extensions, `overlay`, WebSocket, etc.) works identically over TLS with no code
changes — see `examples/nhttpd/main.cpp` for a listener that serves both plain HTTP and HTTPS.

## Reverse proxy

```cpp
using nhttp::server::upstream;

std::vector<upstream> upstreams{
	upstream(endpoint(ip_address::loopback_v4(), 9001)),
	upstream(endpoint(ip_address::loopback_v4(), 9002)),
};

srv.extends(reverse_proxy_for("/api", upstreams)); // plain round-robin across both
```

Mounts one or more upstreams under a URL prefix (the same `vpath` mounting `router` uses) and
relays HTTP/1.1 requests/responses verbatim, including a WebSocket upgrade (passed through as a
raw byte splice once the upstream answers `101`). For an HTTPS upstream, set `use_tls = true`
and `tls_sni_hostname` on the `upstream` entry (`verify_tls_cert` defaults to on). Each proxied
request opens a fresh upstream connection — no connection pooling or active health checking yet
(a down upstream fails that one request with `502`); see `CLAUDE.md`'s Phase 13 log for the
full list of current simplifications.

## HTTP/2

Negotiated automatically over plaintext via **prior knowledge** — no code changes needed beyond
what's already shown above; `listener` detects an HTTP/2 client connection preface on any
plaintext connection and routes it to the HTTP/2 driver instead of HTTP/1.1:

```bash
curl --http2-prior-knowledge http://127.0.0.1:8080/whoami
```

Every extension, the router, and `reverse_proxy` work identically over HTTP/2 with zero code
changes — a stream's request is dispatched through the exact same `listener::dispatch()` path
HTTP/1.1 uses. **ALPN-negotiated HTTP/2 over TLS is not implemented in this round** (deferred
alongside a couple of other real scope cuts — request bodies are fully buffered before dispatch,
response headers are assumed to fit in one `HEADERS` frame, and a few SETTINGS aren't enforced;
see `CLAUDE.md`'s Phase 14 log for the complete, honest list). QUIC/HTTP-3 is not implemented at
all and isn't planned for this project (see `CLAUDE.md`'s architecture-decisions log for why).

## Windows

Windows is a fully supported, natively-tested target (not best-effort) since the platform layer
was hardened in a dedicated pass — see `CLAUDE.md`'s Phase 12 log for the real bugs found
bringing up the IOCP reactor (a couple of them are genuinely useful "gotchas" for anyone doing
Windows socket programming, not just libnhttp-specific). One real, permanent behavioral
difference from Linux: Windows has no `SO_REUSEPORT` equivalent, so `listener` falls back to a
single accept loop that explicitly round-robins connections across workers there (see the
architecture overview above) — functionally equivalent, just not kernel-balanced.

## Benchmarks

Static-file throughput was measured against nginx 1.24 and Apache 2.4.58 (event MPM) using
[wrk](https://github.com/wgtx/wrk), serving an identical deterministic 10&nbsp;KB HTML file, 8
threads / 200 connections / 30s. Apache's stock `MaxRequestWorkers` (150) was raised to 800
before measuring — the default caps concurrency well below anything a production deployment
would run, and left unraised it produced socket errors under this load rather than a meaningful
number. **nhttpd needs no such tuning**: every number below is the untouched library default
(`blocking_pool_size` = 4) — see `CLAUDE.md`'s Phase 16 log for why a small pool is now *better*
than a large one, the opposite of the advice an earlier round of this benchmark gave.

**Two real bugs in this library were found and fixed while running this benchmark** (both now
fixed on `main`, numbers below reflect the fixed build): a systemic `task<T>` coroutine-frame leak
on every `co_await` (~24&nbsp;KB/request under sustained load), and a `SIGPIPE` from a client
resetting its connection killing the entire server process. See `CLAUDE.md`'s Phase 15 log for the
full root-cause story of each.

### Loopback (same-kernel), current `main`

| Server | Req/s | Avg latency | p50 | p99 |
|---|---:|---:|---:|---:|
| nginx 1.24 | 124,984 | 3.13 ms | 1.04 ms | 27.14 ms |
| Apache 2.4.58 (event MPM, tuned) | 34,696 | 11.13 ms | 6.00 ms | 95.80 ms |
| **nhttpd (this repo, untuned default)** | **57,964** | **4.11 ms** | **2.89 ms** | **24.45 ms** |

nhttpd went from 13.8K req/s (the state this Benchmarks section originally documented) to 58K
req/s on this exact benchmark — a ~4.2× improvement, now clearly ahead of Apache and at roughly
half of nginx's throughput, instead of a tenth of it. This came from a `sendfile(2)` fast path for
whole-file responses, cutting redundant thread-pool round trips, a rebuilt lock-free `thread_pool`
job queue (three iterations — two measured as regressions and reverted before the third one
stuck), and a windowed `mmap` read path for the requests sendfile can't take (byte-`Range`, TLS,
chunked). `blocking_pool_size` needs no manual tuning anymore — the library's own small default is
now the fastest setting, the opposite of what this section used to recommend. The full account,
including what was tried and reverted along the way and why, lives in `CLAUDE.md`'s Phase 16 log;
`PLAN.md` tracks only what's still open (a Windows equivalent of the sendfile path, and real
`perf`-based profiling on a host that can run it).

### Docker network-stack benchmark

A same-host, same-kernel loopback benchmark understates real-world overhead: Linux's loopback
interface skips large parts of the normal socket-to-NIC path (no real Ethernet framing, no
driver queueing, and often no checksum work). `benchmark/docker/` runs the same three servers as
separate containers on one Docker bridge network, driven by a fourth client container issuing
`wrk` against each server by its container DNS name — every request crosses a real veth pair and
Linux bridge, the same kernel code paths a real NIC deployment exercises. All three server
containers get identical `cpus`/`mem_limit` resource caps so none has an unfair advantage.

Reproduce it:

```bash
cd benchmark/docker
docker compose build
docker compose up -d bench-nginx bench-apache bench-nhttp
docker compose run --rm bench-client
```

Results (4 CPUs / 1&nbsp;GiB per server container, otherwise identical parameters to the loopback
run above), current `main`, nhttpd still at its untuned default:

| Server | Req/s | Avg latency | p50 | p99 |
|---|---:|---:|---:|---:|
| nginx 1.24 | 55,171 | 5.31 ms | 2.71 ms | 34.20 ms |
| Apache 2.4.58 (event MPM, tuned) | 21,558 | 19.36 ms | 9.65 ms | 135.37 ms |
| **nhttpd (this repo, untuned default)** | **35,869** | **6.53 ms** | **4.62 ms** | **34.00 ms** |

Same story as the loopback numbers: nhttpd (11,170 → 35,869 req/s, a ~3.2× improvement) clearly
beats Apache here too, and sits at roughly two-thirds of nginx's throughput instead of a sixth.
All three containers stayed up and memory-stable for the full run — nhttpd used only
**4.96&nbsp;MiB RSS across 13 threads** after 1.08M requests, both figures lower than nginx's own
(9 processes/threads, 16.96&nbsp;MiB) and far lower than Apache's (199 processes, 26.93&nbsp;MiB) —
confirming the coroutine-leak fix holds under real containerized network traffic, not just
loopback, and that the small default thread/worker counts P1–P4 arrived at are a genuine resource
efficiency, not just a throughput number.

### Scenario 3: a dynamic, per-request read-modify-write endpoint (PHP comparison)

Both benchmarks above serve a static file — a case nhttpd optimized heavily for (the `sendfile(2)`
fast path). This scenario compares a genuinely dynamic endpoint instead: one that reads an integer
out of a file, increments it, writes it back, and responds with the new value, on every single
request. nhttpd's own handler (`benchmark/docker/nhttp/bench_counter_main.cpp`) does this in
native C++ (offloaded to `thread_pool`, guarded by a `std::mutex`); nginx+PHP-FPM and
Apache+mod_php run an equivalent script (`benchmark/docker/php/counter.php`, PHP 8.3, guarded by
`flock()`) doing the same read-increment-write against the same kind of file. This is deliberately
**Docker-only** (`benchmark/docker/`, `--profile scenario3`) — there's no loopback variant, since
the point is a fair, apples-to-apples comparison of three real server stacks under the same
containerized network path used above, not a raw-syscall microbenchmark.

Reproduce it:

```bash
cd benchmark/docker
docker compose build bench-nginx-php bench-apache-php bench-nhttp-counter
docker compose --profile scenario3 up -d bench-nginx-php bench-apache-php bench-nhttp-counter
docker compose run --rm bench-client-scenario3
```

Results (4 CPUs / 1&nbsp;GiB per server container, 8 threads / 200 connections / 30s, same as the
scenarios above):

| Server | Req/s | Avg latency | p50 | p99 |
|---|---:|---:|---:|---:|
| nginx 1.24 + PHP-FPM 8.3 | 3,085 | 86.50 ms | 58.81 ms | 475.23 ms |
| Apache 2.4 (mpm_prefork) + mod_php 8.3 | 3,474 | 79.82 ms | 46.60 ms | 527.78 ms |
| **nhttpd (this repo, untuned default)** | **53,619** | **4.59 ms** | **3.05 ms** | **28.97 ms** |

nhttpd is **~15–17× faster** here than either PHP stack — a much larger gap than the static-file
scenarios, and an expected one: this isn't measuring nginx/Apache's own request handling so much as
the cost of dispatching into an interpreted scripting layer on every request (PHP-FPM's FastCGI
round-trip, or mod_php's in-process interpreter invocation) versus nhttpd's handler running as
plain compiled C++ in the same process that already owns the connection. Apache logged 91 socket
timeouts out of ~104.7K requests (0.09%) under this load; nginx+PHP-FPM had none. All three
counters were cross-checked after each run to confirm no lost updates under concurrency (the
`std::mutex`/`flock()` serialization on each side held).

## Design documents

* [CONCEPTS.md](CONCEPTS.md) — the design philosophy and invariants carried forward from the
  original implementation, and the gaps (multipart parsing, WebSocket frames, TLS) it left open,
  all since closed in this rewrite.
* [USAGE.md](USAGE.md) — the original implementation's observable usage patterns, used as the
  spec for "does the new API still let a caller express the same intent."
* [docs/protocol-extensibility.md](docs/protocol-extensibility.md) — a review of the design
  against the architectural seams HTTP/2 needed (all three held unchanged) and QUIC still would.
* [PLAN.md](PLAN.md) — the live to-do list of open performance follow-ups; finished work is
  removed from it once done (its history lives in `CLAUDE.md`'s phase log instead).
* [CLAUDE.md](CLAUDE.md) — the running build/architecture/decisions log for anyone (human or
  otherwise) picking up work on this repo.
