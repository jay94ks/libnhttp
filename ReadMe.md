# libnhttp

**Language**: English | [한국어](ReadMe.ko.md)

<p align="center">
<img src="https://raw.githack.com/jay94ks/libnhttp/main/logo.png" />
</p>

An event-driven, coroutine-based HTTP/1.1 server library for C++20, built on a multi-threaded
epoll reactor. Linux only for now (Windows support is deliberately deferred — see
[CLAUDE.md](CLAUDE.md)).

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

* Linux (epoll-based reactor; no Windows support in this round)
* GCC ≥ 11 or Clang ≥ 14 (C++20 coroutines)
* CMake ≥ 3.20

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

The library builds warning-free under `-Wall -Wextra -Wpedantic` (plus several more, see
`cmake/CompilerWarnings.cmake`) — this is a hard requirement, not aspirational.

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
src/platform/    epoll, raw sockets, ipv4/ipv6 endpoints
src/async/       task<T> (coroutines), io_context (the reactor), io_context_pool
                 (SO_REUSEPORT multi-threading), thread_pool (blocking-work offload)
src/io/          async stream interface + memory/file/range/socket streams
src/protocol/    header/method/status/mime/date/query-string/resource parsing,
                 chunked transfer codec, multipart/form-data streaming parser
src/server/      listener, HTTP/1.1 connection (one coroutine, no state-machine enum),
                 request/response, the extension registry, vhost/vpath/overlay/single_file
src/router/      the REST router: path trie, fluent registration DSL, middleware, grouping
src/ws/          WebSocket handshake + real RFC 6455 frame I/O
```

A `listener` runs one `io_context` per worker thread; each worker binds its own
`SO_REUSEPORT` socket per listening endpoint, so the kernel — not this library — load-balances
accepted connections across threads. A connection stays on whichever worker accepted it for its
entire lifetime.

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

## Design documents

* [CONCEPTS.md](CONCEPTS.md) — the design philosophy and invariants carried forward from the
  original implementation, and the gaps (multipart parsing, WebSocket frames, TLS) it left open.
* [USAGE.md](USAGE.md) — the original implementation's observable usage patterns, used as the
  spec for "does the new API still let a caller express the same intent."
* [docs/protocol-extensibility.md](docs/protocol-extensibility.md) — a review of the current
  design against the architectural seams a future HTTP/2/QUIC implementation would need.
* [CLAUDE.md](CLAUDE.md) — the running build/architecture/decisions log for anyone (human or
  otherwise) picking up work on this repo.
