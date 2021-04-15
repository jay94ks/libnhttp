# libnhttp

<p align="center">
<img src="https://raw.githack.com/jay94ks/libnhttp/main/logo.png" />
</p>

Event-driven, streaming HTTP server implementation written in C++.

This HTTP library provides re-usable implementations for an HTTP server in C++ for `Windows` and `POSIX` operating systems.
Currently, This only supports HTTP/1.1 and does not support SSL yet, but it will be implemented soon.

Launchpad page: https://launchpad.net/libnhttp

**Table of contents**

* [License](#license)
* [Dependency Licenses](#dependency-licenses)
* [Code Coverage](#code-coverage)
* [Quickstart example](#quickstart-example)
* [Pure Socket Server Library](#pure-socket-server-library)
	* [Socket](#socket)
	* [Socket Watcher](#socket-watcher)
	* [Base class for Listeners](#base-class-for-listeners)
	* [Base class for Accepted Sessions](#base-class-for-accepted-sessions)
* [HTTP Server Library](#http-server-library)
	* [Http Listener](#http-listener)
	* [Http Context](#http-context)
		* [Http Request](#http-request)
		* [Http Response](#http-response)
	* [Http Taggable](#http-taggable)
	* [X Framework](#x-framework)
		* [xfwk_router extension](#xfwk_router-extension)
		* [xfwk_facade interface](#xfwk_facade-interface)
		* [xfwk_middleware interface](#xfwk_middleware-interface)
		* [xfwk_target interface](#xfwk_target-interface)

## License
```
MIT License

Copyright (c) 2021 Jay (jay94ks@gmail.com)
Copyright (c) 2021 neurnn corp

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

## Dependency Licenses
1. utfcpp: http://utfcpp.sourceforge.net/ (Boost Software License)
2. wepoll: https://github.com/piscisaureus/wepoll (Copyright: Custom)
3. sha1: https://github.com/983/SHA1 (Unlicense)

both licenses are MIT compatible, but follows each library's license.

**PLEASE READ THEIR LICENSE FILES**
1. utfcpp: https://github.com/jay94ks/libnhttp/blob/main/libnhttp/nhttp/depends/utf8.h
2. wepoll: https://github.com/jay94ks/libnhttp/tree/main/libnhttp/nhttp/depends/wepoll
3. sha1: https://github.com/jay94ks/libnhttp/tree/main/libnhttp/nhttp/depends/sha1/LICENSE

## Code Coverage
1. Line coverage: https://raw.githack.com/jay94ks/libnhttp/main/coverage/index.html

## Quickstart example
```
#include <nhttp/nhttp.hpp>
#include <nhttp/server/http_server.hpp>

#ifdef _MSC_VER
#	pragma comment(lib, "libnhttp.lib")
#endif

using namespace nhttp;
using namespace nhttp::server;

int main(int argc, char** argv) {
	socket_watcher watcher(1024);
	http_listener listener(watcher, http_params());

	if (!listener.with(ipv4::resolve("127.0.0.1", 8080))) {
		std::cout << "error: can't listen: 127.0.0.1:8080.\n";
		return 1;
	}

	if (!listener.with(ipv6::resolve("::1", 8080))) {
		std::cout << "error: can't listen: [::1]:8080.\n";
		return 1;
	}

	auto example_com = vhost_for("www.example.com");
	listener.extends(example_com);

	/* set directory overlay on default host */
	listener.extends(overlay_of(".", "index.html"));
    
    /* set directory overlay on example.com. */
    example_com->extends(overlay_of("./example.com", "index.html"));

	/* main thread as HTTP Worker thread. */
	for(http_context_ptr context : listener) {
		context->response = make_response("hello world");
		context->close();
	}

	/* main thread as event dispatcher (#1).*/
	listener.run();
    
    /* main thread as event dispatcher (#2).
    listener.run([](socket_event* event) {
    	/* co-routines for co-watching sockets. */
    });
    
    /* main thread as event dispatcher (#3). */
    for (socket_event& event : watcher) {
    	/* co-routines for co-watching sockets. */
    }
	
	return 0;
}
```

## Pure Socket Server Library
### Socket
 Socket implementation that supports multiple platforms.
```
socket_t sock = socket_t::create<ipv4_addr, tcp>();
hal::socket_raw_t raw = sock.get_raw();

if (!sock) {
	return false;
}

if (!sock.bind(ipv4::resolve("0.0.0.0:8080")) || !sock.listen(10)) {
	sock.close();
	return false;
}

raw.set_non_blocking(true);
raw.set_reuse_address(true);
```

### Socket Watcher
Socket Watcher is an interface that observes socket events and notifies them with a callback or checks them with a range-loop.
```
socket_watcher watcher(1024);

watcher.watch(sock, [](socket_t sock) {
	if (sock.can_read()) {
		/* ... */
	}
});
```

with range loop:
```
socket_watcher watcher(1024);
watcher.watch(sock, nullptr);

for(socket_event& event : watcher) {
	/* ... */
}
```

### Base class for Listeners
`listener_base` provides a unified interface when implementing a protocol.
```
/**
 * class http_raw_listener.
 * http listener for accepting http sessions.
 */
class http_raw_listener : public base::listener_base {
	friend class http_raw_link;

private:
	http_params params;
	std::shared_ptr<http_chunked_alloc> chunk_alloc;

public:
	http_raw_listener(const socket_watcher& watcher, const http_params& params);
	virtual ~http_raw_listener() { terminate(); }

public:
	inline const http_params& get_params() const { return params; }

protected:
	/* allocate a http link. */
	virtual base::session_base* on_enter() override;

	/* de-allocate a http link. */
	virtual void on_leave(base::session_base* link) override;

protected:
	virtual void on_raw_context(const std::shared_ptr<http_raw_context>& context, bool has_error) = 0;
};
```

### Base class for Accepted Sessions
```
/**
 * class session_base.
 * base class of a session.
 */
class NHTTP_API session_base {
	friend class listener_base;

public:
	session_base() { }
	virtual ~session_base() { }

protected:
	socket_t socket;
	std::shared_ptr<asyncs::context> asyncs;

public:
	/* get socket instance. */
	inline const socket_t& get_socket() const { return socket; }

	/* get async worker. */
	inline const std::shared_ptr<asyncs::context>& get_asyncs() const { return asyncs; }

protected:
	/* called when the session should be initialized with socket. */
	virtual void on_initiate(const socket_t& socket, const std::shared_ptr<asyncs::context>& asyncs) {
		this->socket = socket;
		this->asyncs = asyncs;
	}

	/* called when the session should be de-initialized.*/
	virtual void on_finalize() {
		this->socket = socket_t();
		this->asyncs = nullptr;
	}

	/**
	 * called when the session can read or write a socket.
	 * @returns:
	 *  = true : keep session alive.
	 *  = false: kill session.
	 */
	virtual bool on_event() = 0;
};
```

## HTTP Server Library
### Http Listener
`http_listener` class is for listening `http` port and it accepts clients  from configured ports and encapsulates them to `http_raw_session`s.
```
socket_watcher watcher(1024);
http_listener listener(watcher, http_params());

if (!listener.with(ipv4::resolve("127.0.0.1", 8080))) {
	std::cout << "error: can't listen: 127.0.0.1:8080.\n";
	return 1;
}

if (!listener.with(ipv6::resolve("::1", 8080))) {
	std::cout << "error: can't listen: [::1]:8080.\n";
	return 1;
}
```

### Http Context
`http_context` is the 2nd encapsulation of `http_raw_request` and `http_raw_response` created from the original bytes received from `http_raw_session`. it is used for reading the request and its body, and writing the response.
```
const char* if_match = ltrim(request_headers.get(http_header::IF_MATCH));
const char* if_none_match = ltrim(request_headers.get(http_header::IF_NONE_MATCH));
const char* if_modified_since = ltrim(request_headers.get(http_header::IF_MODIFIED_SINCE));
const char* if_range = ltrim(request_headers.get(http_header::IF_RANGE));

/* set necessary headers. */
response_headers.set(http_header::ACCEPT_RANGES, "bytes");
response_headers.set(http_header::ETAG, http_etag);
response_headers.set(http_header::LAST_MODIFIED, http_mtime);

/* set cache-control. */
if ( request_headers.isset(http_header::AUTHORIZATION))
	 response_headers.set(http_header::CACHE_CONTROL, "private, must-revalidate");
else response_headers.set(http_header::CACHE_CONTROL, "public, must-revalidate");
```

#### Http Request
`http_request` is the 2nd encapsulation of `http_raw_request`. this is for getting request headers and its body.
```
const char* if_match = ltrim(request_headers.get(http_header::IF_MATCH));
```

#### Http Response
`http_request` is the 2nd encapsulation of `http_raw_request`. this is for setting response headers and its body.
```
context->response->headers.set(http_header::ACCEPT_RANGES, "bytes");
```
and setting contents:
```
context->response = make_response(404); /* 404 Not Found. */
context->response = make_response("Hello world!"); /* 200 OK, and content: Hello world! (text/html) */
context->response = make_response("{ \"hello\": \"world\" }", http_mime_type::APPLICATION_JSON);
// --> 200 OK, and content: { "hello": "world" } (application/json).
```

### Http Taggable
`http_request` and `http_link` are inherited from `http_taggable` interface. and they can have tags which discribe handling status.
```
/**
 * class http_vhost_tag.
 * tag for marking the request is handled by.
 */
class NHTTP_API http_vhost_tag {
public:
	std::stack<http_vhost*> vhosts;
};

bool http_vhost::on_enter(std::shared_ptr<http_context> context) {
	http_vhost_tag* tag = context->link->ensured_tag<http_vhost_tag>();

	/* set tag for notifying current vhost. */
	tag->vhosts.push(this);

	return true;
}

void http_vhost::on_leave(std::shared_ptr<http_context> context) {
	http_vhost_tag* tag = context->link->get_tag_ptr<http_vhost_tag>();

	if (tag) {
		tag->vhosts.pop();
	}
}
```

### X Framework
X-Framework is for writing REST API or using MVC pattern easily in C++.
Basically, `libnhttp` doesn't parse anything from request, 
excluding query string. just `http_extension` and `http_listener`, `event loop` only are there.

**Writing a Web service under that API, it's so tired.**

So, I've prepared a simple framework for that easily works.

#### xfwk_router extension
Router is root node of `xfwk_route` and it is a `http_extension`, implements `xfwk_facade`.

```
auto router = std::make_shared<xfwk_router>();

/* it should be `std::shared_ptr` for registering it as extension. */
listener.extends(router);

/* or you can register it on some vhost. */
example_com.extends(router);

/* of course, twice are available. */
example_xyz.extends(router);
```

**Laravel-like router facades** allows you write API easily.
```
/* registers '/whoami' `path` handler for GET method. */
router->get("whoami", target_by([](http_request_ptr) {
	return make_response("I'm jay.");
}));

/* registers '/:user' path parameter handler for GET method. */
router->get("/:user", target_by([](http_request_ptr req) {
	std::string user = route_of(req).captures[":user"];
	return make_response(user + " is ...");
}));

/* registers '/:user' path parameter handler for POST method. */
router->post("/:user", target_by([](http_request_ptr req) {
	std::string user = route_of(req).captures[":user"];
	std::string body;

	/* request body is `binary` stream object. */
	if (!req->get_request_body()->read_all(body))
		return make_response(400);
		
	return make_response(user + " says " + body);
}));

/* and the parameters can be filtered by predicate: */
/* and if the request filtered out, it will be fail down to next node. */
router->param("/:user", [](const std::string& value) {
	/* user name should be jay or kay. */
	return value == "jay" || value == "kay";
});

router->any("/:any", [](http_request_ptr) {
	return make_response("he (or she) isn't allowed to read this page!");
});
```

#### xfwk_facade interface
Detail of the `xfwk_facade` is:
```
class NHTTP_API xfwk_facade {
public:
	virtual ~xfwk_facade() { }

protected:
	/* get route of base path. */
	virtual std::shared_ptr<xfwk_route> get_route() const = 0;

	/* get or set middleware stack. @warn: DO NOT set under routing process! */
	virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const = 0;
	virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) = 0;

public:
	/* set target to HEAD, OPTIONS, GET, POST, PUT, DELETE, PATCH methods. */
	virtual this_ptr<xfwk_facade> any(std::shared_ptr<xfwk_target> target);
	inline this_ptr<xfwk_facade> head(std::shared_ptr<xfwk_target> target)								{ return method(http_method::HEAD, target); }
	inline this_ptr<xfwk_facade> options(std::shared_ptr<xfwk_target> target)							{ return method(http_method::OPTIONS, target); }
	inline this_ptr<xfwk_facade> get(std::shared_ptr<xfwk_target> target)								{ return method(http_method::GET, target); }
	inline this_ptr<xfwk_facade> post(std::shared_ptr<xfwk_target> target)								{ return method(http_method::POST, target); }
	inline this_ptr<xfwk_facade> put(std::shared_ptr<xfwk_target> target)								{ return method(http_method::PUT, target); }
	inline this_ptr<xfwk_facade> patch(std::shared_ptr<xfwk_target> target)								{ return method(http_method::PATCH, target); }
	inline this_ptr<xfwk_facade> delet(std::shared_ptr<xfwk_target> target)								{ return method(http_method::DELETE, target); }

	/* set target for path to HEAD, OPTIONS, GET, POST, PUT, DELETE, PATCH methods. */
	virtual this_ptr<xfwk_facade> any(const std::string& path, std::shared_ptr<xfwk_target> target);
	inline this_ptr<xfwk_facade> head(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::HEAD, path, target); }
	inline this_ptr<xfwk_facade> options(const std::string path, std::shared_ptr<xfwk_target> target)	{ return method(http_method::OPTIONS, path, target); }
	inline this_ptr<xfwk_facade> get(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::GET, path, target); }
	inline this_ptr<xfwk_facade> post(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::POST, path, target); }
	inline this_ptr<xfwk_facade> put(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::PUT, path, target); }
	inline this_ptr<xfwk_facade> patch(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::PATCH, path, target); }
	inline this_ptr<xfwk_facade> delet(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::DELETE, path, target); }

	/* set target for specific method. */
	virtual this_ptr<xfwk_facade> method(const http_method& method, std::shared_ptr<xfwk_target> target);
	virtual this_ptr<xfwk_facade> method(const http_method& method, const std::string& path, std::shared_ptr<xfwk_target> target);

	/* get target of specific method. */
	std::shared_ptr<xfwk_target> method(const http_method& method);
	std::shared_ptr<xfwk_target> method(const http_method& method, const std::string& path);

	/* set parameter's test predicate. */
	this_ptr<xfwk_facade> param(const std::string& name, lambda_t<bool(const std::string&)> predicate);

	/* prepend or append a middleware on the stack. */
	virtual this_ptr<xfwk_facade> prepend(std::shared_ptr<xfwk_middleware> middleware);
	virtual this_ptr<xfwk_facade> append(std::shared_ptr<xfwk_middleware> middleware);

	/* make a grouped route. */
	virtual std::shared_ptr<xfwk_facade> group(lambda_t<void(std::shared_ptr<xfwk_facade>)> lambda);
};
```

#### xfwk_middleware interface
This is an interface for filtering or pre-handling an request before real handler.

```
class NHTTP_API xfwk_middleware {
	friend class xfwk_middleware_stack;

public:
	typedef http_response_ptr(* next_type)(http_request_ptr request);
	virtual ~xfwk_middleware() { }

public:
	/* handle target using this middleware. */
	http_response_ptr handle_for(http_request_ptr request, const std::shared_ptr<xfwk_target>& target) const;

protected:
	/* handle target. */
	virtual http_response_ptr handle(http_request_ptr request, next_type next) const = 0;
};
```

As a example, you can read `xfwk_middleware_stack` class.

1. https://github.com/jay94ks/libnhttp/blob/main/libnhttp/nhttp/server/xfwk/xfwk_middleware.hpp
2. https://github.com/jay94ks/libnhttp/blob/main/libnhttp/nhttp/server/xfwk/xfwk_middleware.cpp

#### xfwk_target interface
This is an interface for implementing the real handler for the end-point.
You can inherit it yourself or use already implemented targets.

```
class NHTTP_API xfwk_target : public xfwk_facade_middlewares {
private:
	std::shared_ptr<xfwk_middleware_stack> middlewares;
		
public:
	virtual ~xfwk_target() { }

public:
	/**
		* -- xfwk_facade_middlewares interface --
		* get or set middleware stack. @warn: DO NOT set under routing process!
		*/
	virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const override { return middlewares; }
	virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) override { this->middlewares = middlewares; }

public:
	/* determines this target is unified or not. */
	virtual bool is_unifed() const { return false; }

	/* handle request and generate response. */
	virtual http_response_ptr handle(http_request_ptr request) const = 0;
};
```

**already implemented targets:**
```
/* 1. Lambda as target. */
target_by([](http_request_ptr req) {
	/* ... */
});

/* 2. Method as target. */
class my_controller {
public:
	http_response_ptr hello(http_request_ptr p) {
		return make_response("hello world!");
	}
};

target_by(my_ctrl, &my_controller::hello)

/* 3. Unified target. */
auto unified = std::make_shared<xfwk_unified_target>();

unified->set_target_for(http_method::GET, target_by(...));
```
