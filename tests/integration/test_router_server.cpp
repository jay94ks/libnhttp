#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "nhttp/router/router.hpp"
#include "../support/raw_http_client.hpp"

#include <thread>

using namespace nhttp::server;
using namespace nhttp::platform;
using namespace nhttp::router;
using namespace nhttp::async;
using nhttp_test::raw_http_client;

namespace {

	struct running_server {
		listener srv;
		std::thread thread;

		running_server() : srv(make_params()) {
			REQUIRE(srv.listen(endpoint(ip_address::any_v4(), 0)));
			thread = std::thread([this] { srv.run(); });
		}

		~running_server() {
			srv.stop();
			thread.join();
		}

		std::uint16_t port() const { return srv.local_endpoint()->port(); }

	private:
		static params make_params() {
			params p;
			p.io_worker_count = 2;
			p.blocking_pool_size = 1;
			return p;
		}
	};

	class add_header_middleware final : public middleware {
	public:
		task<response> handle(request& req, next_fn next) const override {
			response r = co_await next(req);
			r.headers.set("X-Middleware", "applied");
			co_return r;
		}
	};

}

TEST_CASE("router matches a fixed path, a captured parameter, and promotes multiple methods on one path", "[integration][router]") {
	running_server server;
	auto r = make_router();

	r->get("whoami", target_by([](request&) { return make_response("I'm jay."); }));

	r->get("/:user", target_by([](request& req) {
		return make_response(route_of(req).captures.at(":user") + " is ...");
	}));

	r->post(":user/set", target_by([](request& req) -> task<response> {
		std::string body;
		co_await req.body->read_all(body);
		co_return make_response(route_of(req).captures.at(":user") + " says " + body);
	}));

	r->put(":user/set", target_by([](request& req) -> task<response> {
		std::string body;
		co_await req.body->read_all(body);
		co_return make_response("updated " + route_of(req).captures.at(":user") + ": " + body);
	}));

	server.srv.extends(r);

	raw_http_client c1(ip_version::v4, server.port());
	auto whoami = c1.send("GET /whoami HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(whoami.status == 200);
	REQUIRE(whoami.body == "I'm jay.");

	raw_http_client c2(ip_version::v4, server.port());
	auto profile = c2.send("GET /jay HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(profile.status == 200);
	REQUIRE(profile.body == "jay is ...");

	raw_http_client c3(ip_version::v4, server.port());
	const std::string set_body = "hello=world";
	auto post_set = c3.send("POST /jay/set HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
		std::to_string(set_body.size()) + "\r\nConnection: close\r\n\r\n" + set_body);
	REQUIRE(post_set.status == 200);
	REQUIRE(post_set.body == "jay says hello=world");

	raw_http_client c4(ip_version::v4, server.port());
	auto put_set = c4.send("PUT /jay/set HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
		std::to_string(set_body.size()) + "\r\nConnection: close\r\n\r\n" + set_body);
	REQUIRE(put_set.status == 200);
	REQUIRE(put_set.body == "updated jay: hello=world");

	raw_http_client c5(ip_version::v4, server.port());
	auto wrong_method = c5.send("DELETE /jay/set HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(wrong_method.status == 405); // path matched (GET/POST/PUT exist), but not DELETE
}

TEST_CASE("router param() predicate constrains which values a parameter accepts", "[integration][router]") {
	running_server server;
	auto r = make_router();

	r->get("/:user", target_by([](request& req) {
		return make_response(route_of(req).captures.at(":user") + " is allowed");
	}));
	r->param(":user", [](std::string_view v) { return v == "jay" || v == "kay"; });

	server.srv.extends(r);

	raw_http_client allowed(ip_version::v4, server.port());
	auto ok = allowed.send("GET /kay HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(ok.status == 200);
	REQUIRE(ok.body == "kay is allowed");

	raw_http_client rejected(ip_version::v4, server.port());
	auto not_ok = rejected.send("GET /someone-else HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	// the router is mounted at "/" and so owns this whole path space: a rejected
	// predicate means no route matched, which the router (via vpath) reports as
	// its own 404 rather than declining for the listener's fallback to handle.
	REQUIRE(not_ok.status == 404);
}

TEST_CASE("router group() applies prepended middleware to every route registered inside it", "[integration][router]") {
	running_server server;
	auto r = make_router();

	r->get("outside", target_by([](request&) { return make_response("outside"); }));

	r->group([](facade_ptr inner) {
		inner->get("inside-a", target_by([](request&) { return make_response("inside-a"); }));
		inner->get("inside-b", target_by([](request&) { return make_response("inside-b"); }));
	})->prepend(std::make_shared<add_header_middleware>());

	server.srv.extends(r);

	raw_http_client c1(ip_version::v4, server.port());
	auto outside = c1.send("GET /outside HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(outside.status == 200);
	REQUIRE(outside.header("X-Middleware").empty());

	raw_http_client c2(ip_version::v4, server.port());
	auto inside_a = c2.send("GET /inside-a HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(inside_a.status == 200);
	REQUIRE(inside_a.header("X-Middleware") == "applied");

	raw_http_client c3(ip_version::v4, server.port());
	auto inside_b = c3.send("GET /inside-b HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(inside_b.status == 200);
	REQUIRE(inside_b.header("X-Middleware") == "applied");
}
