# libnhttp — Usage Patterns (from the original implementation)

**Language**: English | [한국어](USAGE.ko.md)

This document catalogs the **observable usage surface** of the original libnhttp — the shapes
of code a consumer of the library actually wrote — extracted from `ReadMe.md`, `nhttpd/main.cpp`,
and `libnhttp-tests/main.cpp`. It exists so the rewrite's new API can be judged against
"does this still let a caller express the same intent," without being bound to the old
signatures verbatim.

## 1. Bootstrapping a server

```cpp
socket_watcher watcher(1024);                       // reactor, sized by max fds
http_listener listener(watcher, http_params());      // one listener, shared or own reactor

listener.with(ipv4::resolve("127.0.0.1", 8080));      // bind + listen, per address family
listener.with(ipv6::resolve("::1", 8080));

listener.run();                                       // block current thread as event loop
```

Variants used elsewhere:
- `run(lambda)` — caller supplies a co-loop lambda invoked once per idle tick and once per
  ready event, returning `false` to stop the loop (used for graceful shutdown via an atomic
  flag / `/exit` endpoint in tests).
- A range-for over the raw `socket_watcher` / `listener` itself for "main thread as one of N
  worker threads pulling ready contexts" (`for (http_context_ptr ctx : listener)`).
- Multiple listeners can share one `socket_watcher` (co-hosted) or each own one (self-hosted).

**Feature to preserve:** the caller decides whether the main thread blocks entirely in
`run()`, drives a custom loop body, or is just one of several consumers pulling ready
work — the library doesn't assume it owns the thread.

## 2. Responding to a request

```cpp
context->response = make_response("hello world");                         // 200, text/html
context->response = make_response(404);                                    // status-only
context->response = make_response("{...}", http_mime_type::APPLICATION_JSON);
context->close();                                                           // finish/flush
```

Handlers read the body as a stream, not a pre-materialized buffer:
```cpp
std::string body;
if (!req->get_request_body()->read_all(body))
    return make_response(400);
```

**Feature to preserve:** constructing a response from a string, a status code alone, a
string+mime pair, or (elsewhere) a stream/file/byte-range must all be equally simple
one-liners — this is the most common call in any handler, it must stay terse.

## 3. Static file / directory serving

```cpp
listener.extends(overlay_of(".", "index.html"));                 // serve cwd, default index
example_com->extends(overlay_of("./example.com", "index.html")); // per-vhost overlay
```

**Feature to preserve:** conditional GET (ETag/If-None-Match/Last-Modified) and byte-range
requests (`Range:` header, `206 Partial Content`) work automatically for anything served this
way, with zero handler-side code — verified in the Makefile's manual curl-based smoke test
(`Range: bytes=0-5`, `If-None-Match`, `If-Modified-Since` against both IPv4 and IPv6
listeners).

## 4. Virtual hosting

```cpp
auto example_com = vhost_for("www.example.com");   // exact hostname
auto by_regex     = vhost_for(std::regex(".*"));    // pattern-based

listener.extends(example_com);
example_com->extends(some_router_or_overlay);       // extensions nest under a vhost
```

**Feature to preserve:** a vhost is just another extension that narrows "which requests do my
children see" by hostname; anything installable on a listener is installable inside a vhost.

## 5. REST routing (xfwk router)

```cpp
auto router = std::make_shared<xfwk_router>();
listener.extends(router);

router
    ->get("whoami", target_by([](http_request_ptr) {
        return make_response("I'm jay.");
    }))
    ->get("/:user", target_by([](http_request_ptr req) {
        std::string user = route_of(req).captures[":user"];
        return make_response(user + " is ...");
    }))
    ->post("/:user", target_by([](http_request_ptr req) {
        std::string user = route_of(req).captures[":user"];
        std::string body;
        if (!req->get_request_body()->read_all(body))
            return make_response(400);
        return make_response(user + " says " + body);
    }));

// constrain a path parameter:
router->param("/:user", [](const std::string& value) {
    return value == "jay" || value == "kay";
});

// catch-all fallback:
router->any("/:any", [](http_request_ptr) {
    return make_response("not allowed");
});
```

Grouping + middleware, and method chaining on one path (from `libnhttp-tests/main.cpp`):
```cpp
router->group([](xfwk_facade_ptr inner) {
    inner->get(":user/profile", handler_a)
         ->get(":user/greetings", handler_b)
         ->post(":user/set", handler_c);

    inner->put(":user/set", handler_d);          // same path, different method
    inner->delet(":user", handler_e);
    inner->param(":user", [](const std::string& name) {
        return name == "jay" || name == "kay";
    });
});
```

Method-as-target (binding a member function instead of a lambda):
```cpp
class my_controller {
public:
    http_response_ptr hello(http_request_ptr p) { return make_response("hello world!"); }
};

auto my_ctrl = std::make_shared<my_controller>();
router->get(target_by(my_ctrl, &my_controller::hello));
```

**Feature to preserve:** chained registration returning something still-chainable; captured
route parameters retrievable from inside the handler via the request object; per-parameter
validation predicates; grouping a batch of route registrations to later attach shared
middleware; binding either a free lambda or an existing object's member function as a target
with identical call-site ergonomics.

## 6. Manual protocol-level smoke test matrix (from `libnhttp/Makefile`'s `run-test-app`)

This is the closest thing the original project had to an integration test, and it enumerates
the request shapes the server must handle correctly — useful as a checklist for the rewrite's
Catch2 integration tests, run against both `127.0.0.1` and `::1`:

- Plain `GET /` with a query string containing repeated/odd separators
  (`?dummy=query-string&a=1,b=c&d=3`).
- `GET /` with `Range: bytes=0-5` (partial content) and `Range: bytes=0-` (from-offset).
- `GET /` with `If-None-Match: "<etag>"` (expect 304 on match).
- `GET /` with `If-Modified-Since: <date>` (expect 304 on not-modified).
- `GET /whoami`, `GET /always-501` (explicit unhandled-route case, expect 501).
- `POST`, `PUT` with a urlencoded body to a routed path (`/jay/set`), and to the always-501
  path (verifying routing/priority doesn't accidentally handle it).
- `PATCH` to a path that only defines GET/POST/PUT/DELETE (expect 405 Method Not Allowed).
- `DELETE` with **no** body (expect success/handled) vs `DELETE` **with** a body (expect 400 —
  DELETE is not defined to carry a request body).
- `POST` with `Transfer-Encoding: chunked` (chunked body must decode correctly) to an
  **unrouted** path (verify chunked body is still drained/discarded correctly even when no
  handler consumes it).
- `POST`/`PUT` of a large binary body (`Content-Type: application/octet-stream`, an
  `index.html`-sized payload) — round-trips through the fixed-length content handler.
- Uploading and then downloading a large file (~100MB, `dd`/`wget`) through the static overlay
  — exercises the streaming path end-to-end, not just small in-memory bodies.
- Every case above repeated identically against an IPv6 listener (`[::1]:8080`) — dual-stack
  behavior is a first-class requirement, not an afterthought.
- A dedicated `/exit` POST endpoint used purely to terminate the test server process cleanly.

## 7. WebSocket handshake (functional in the original; frame I/O was not — see CONCEPTS.md §6)

```cpp
listener.extends(websock_ep_for("/ws", [](std::shared_ptr<http_websocket> ws) {
    // on_connect: return false to reject the upgrade.
    return true;
}));
```
The handshake (key validation, `Sec-WebSocket-Accept` computation, `101` response) worked;
sending/receiving frames after upgrade did not. Whether the rewrite implements real frame I/O
is an open decision (see CONCEPTS.md §6), not something to copy as-is.

## 8. Tags for cross-cutting per-connection/per-request state

```cpp
struct http_vhost_tag { std::stack<http_vhost*> vhosts; };

// somewhere reusable, attached to the connection (survives across keep-alive requests):
http_vhost_tag* tag = context->link->ensured_tag<http_vhost_tag>();
tag->vhosts.push(this);
```
**Feature to preserve:** any extension can attach its own strongly-typed piece of state to a
connection or a request without modifying a shared context class, and without every extension
needing to know about every other extension's state.
