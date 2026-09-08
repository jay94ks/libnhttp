// nhttpd — the nhttp library's demo/smoke-test server.
//
// Mirrors the original implementation's demo endpoints (see USAGE.md) on the
// new C++20/coroutine API, both as living documentation and as a manual
// smoke-test companion (exercise it with curl the same way the original
// Makefile's `run-test-app` target did — see USAGE.md §6 for the request
// matrix: conditional GET, byte-Range, chunked bodies, 404/405/501, keep-alive,
// and identical behavior over IPv4 and IPv6).
//
// Build: cmake --build build --target nhttpd
// Run:   ./build/examples/nhttpd [directory-to-serve] [port] [tls-cert.pem] [tls-key.pem]
//        (the last two are optional; when given, HTTPS is also served on port+1)

#include "nhttp/server/listener.hpp"
#include "nhttp/server/extensions/overlay.hpp"
#include "nhttp/server/extensions/websocket_endpoint.hpp"
#include "nhttp/router/router.hpp"
#include "nhttp/plugin/plugin.hpp"
#include "nhttp/plugin/plugin_scope.hpp"
#include "nhttp/plugin/plugin_manager.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nhttp;
using namespace nhttp::server;
using namespace nhttp::router;
using namespace nhttp::platform;

namespace {

	async::task<void> echo_websocket(std::shared_ptr<ws::ws_connection> ws) {
		for (;;) {
			std::optional<ws::message> msg = co_await ws->receive();

			if (!msg)
				co_return;

			if (msg->type == ws::message_type::text)
				co_await ws->send_text(msg->data);
			else
				co_await ws->send_binary(msg->data.data(), msg->data.size());
		}
	}

	/* what a real plugin (e.g. one wrapping a MySQL connection pool) would
	 * stash into a request's tag storage in on_begin, for the wrapped handler
	 * below to read back out. */
	struct hit_count_tag {
		std::size_t value = 0;
	};

	/* a trivial stand-in for a real resource-owning plugin (a DB connection
	 * pool, etc.) -- on_init/on_deinit mark server-wide setup/teardown,
	 * on_begin hands the wrapped handler a per-request value via req.tags,
	 * on_end is where a real plugin would release whatever on_begin acquired.
	 * See "/counted" below for how a route opts into this plugin's scope. */
	class counter_plugin final : public plugin::plugin {
	public:
		async::task<void> on_init(listener&, async::io_context&) override {
			std::puts("counter_plugin: on_init");
			co_return;
		}

		async::task<void> on_deinit(listener&, async::io_context&) override {
			std::puts("counter_plugin: on_deinit");
			co_return;
		}

		async::task<void> on_begin(request& req) override {
			req.tags.ensure<hit_count_tag>().value = ++hits_;
			co_return;
		}

	private:
		std::atomic<std::size_t> hits_{ 0 };
	};

}

int main(int argc, char** argv) {
	const std::string serve_dir = argc > 1 ? argv[1] : ".";
	const std::uint16_t port = argc > 2 ? static_cast<std::uint16_t>(std::atoi(argv[2])) : 8080;

	params p;
	listener srv(p);

	// --- static file serving over the current (or given) directory ---
	//
	// IMPORTANT: a router (or any extension built on server::vpath) mounted at
	// "/" always accepts every request in its `wants()` — vpath's prefix check
	// against "/" is trivially true for any path — so it must be given a
	// *higher* priority number (lower precedence, tried later) than anything
	// that should get first refusal, like this overlay or the websocket
	// endpoint below. The numeric defaults do NOT guarantee this ordering by
	// themselves; see CLAUDE.md's architecture-decisions log for the full story
	// (this was discovered writing this very example).
	auto static_files = std::make_shared<overlay>(serve_dir, "index.html", srv.blocking_pool(), 0x10000000u);
	srv.extends(static_files);

	// --- a WebSocket echo endpoint, for exercising real frame I/O manually ---
	srv.extends(websocket_endpoint_for("/ws", echo_websocket)); // default priority, still < router's

	// --- the REST API, mounted last (catch-all "/") ---
	auto api = make_router();

	api->get("whoami", target_by([](request&) {
		return make_response("I'm jay.");
	}));

	api->any("always-501", target_by([](request&) {
		return make_response(501);
	}));

	api->post("exit", target_by([&srv](request&) {
		std::puts("received POST /exit -- shutting down.");
		srv.stop();
		return make_response("server exiting...");
	}));

	// --- plugin system demo: a route scoped to counter_plugin's on_begin/
	// on_end (see its class comment above) ---
	auto counter = std::make_shared<counter_plugin>();
	plugin::plugin_manager plugins;
	plugins.attach(counter);

	api->group([&](facade_ptr inner) {
		inner->get("counted", target_by([](request& req) {
			return make_response("hit #" + std::to_string(req.tags.get<hit_count_tag>()->value));
		}));
	})->prepend(std::make_shared<plugin::plugin_scope>(counter));

	api->group([](facade_ptr inner) {
		inner->get(":user/profile", target_by([](request& req) {
			return make_response(route_of(req).captures.at(":user") + " is ...");
		}));

		inner->get(":user/greetings", target_by([](request& req) {
			return make_response(route_of(req).captures.at(":user") + " says hi!");
		}));

		inner->post(":user/set", target_by([](request& req) -> async::task<response> {
			std::string body;
			co_await req.body->read_all(body);

			if (body.empty())
				co_return make_response(400);

			co_return make_response(std::move(body));
		}));

		inner->put(":user/set", target_by([](request& req) -> async::task<response> {
			std::string body;
			co_await req.body->read_all(body);
			co_return make_response(std::move(body));
		}));

		inner->del(":user", target_by([](request& req) {
			return make_response(route_of(req).captures.at(":user") + " deleted!");
		}));

		inner->param(":user", [](std::string_view name) {
			return name == "jay" || name == "kay";
		});
	});

	srv.extends(api);

	if (!srv.listen(endpoint(ip_address::loopback_v4(), port))) {
		std::fprintf(stderr, "error: can't listen: 127.0.0.1:%u\n", port);
		return 1;
	}

	if (!srv.listen(endpoint(ip_address::loopback_v6(), port))) {
		std::fprintf(stderr, "error: can't listen: [::1]:%u\n", port);
		return 1;
	}

	std::printf("nhttpd listening on 127.0.0.1:%u and [::1]:%u, serving '%s'\n", port, port, serve_dir.c_str());
	std::printf("try: curl http://127.0.0.1:%u/whoami\n", port);
	std::printf("try: curl http://127.0.0.1:%u/counted\n", port);

#ifdef NHTTP_HAVE_TLS
	if (argc > 4) {
		const std::uint16_t tls_port = static_cast<std::uint16_t>(port + 1);

		if (!srv.listen_tls(endpoint(ip_address::loopback_v4(), tls_port), argv[3], argv[4])) {
			std::fprintf(stderr, "error: can't listen (tls): 127.0.0.1:%u (check cert/key paths)\n", tls_port);
			return 1;
		}

		std::printf("nhttpd also listening (TLS) on 127.0.0.1:%u\n", tls_port);
		std::printf("try: curl -k https://127.0.0.1:%u/whoami\n", tls_port);
	}
#endif

	plugins.init_all(srv);
	srv.run(); // blocks until POST /exit calls srv.stop()
	plugins.deinit_all(srv);

	return 0;
}
