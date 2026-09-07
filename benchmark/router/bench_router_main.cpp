// Router-backed benchmark server (see PLAN.md's "router::route_match()'s
// per-candidate backtracking cost" item — the existing loopback/Docker
// benchmarks only ever exercise `overlay`'s static-file path, never the
// router, so this is a dedicated harness to establish a baseline and A/B
// candidate fixes against). Deliberately not examples/nhttpd/main.cpp, for
// the same reason bench_main.cpp isn't: a minimal, fixed request shape,
// nothing else competing for CPU. Not wired into CMake, same as
// benchmark/docker/nhttp/bench_main.cpp — compiled directly against the
// already-built libnhttp.a.
//
// Route shape is deliberately chosen to stress route_match()'s backtracking:
// a static sibling ("admin") competes with a param child (":id") at the same
// trie level (forces the static-child-then-backtrack path on every request
// that doesn't match "admin"), and two more segments of nested params below
// that (":postId", ":commentId") multiply the per-segment route_state copy
// cost the PLAN.md item calls out.
#include "nhttp/server/listener.hpp"
#include "nhttp/router/router.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nhttp;
using namespace nhttp::server;
using namespace nhttp::router;
using namespace nhttp::platform;

int main(int argc, char** argv) {
	const std::uint16_t port = argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 8080;

	params p;
	listener srv(p);

	auto api = make_router();

	api->get("users/admin", target_by([](request&) {
		return make_response("admin");
	}));

	api->get("users/:id/profile", target_by([](request& req) {
		return make_response(route_of(req).captures.at(":id") + "'s profile");
	}));

	api->get("users/:id/posts/:postId", target_by([](request& req) {
		const route_state& st = route_of(req);
		return make_response(st.captures.at(":id") + "/" + st.captures.at(":postId"));
	}));

	api->get("users/:id/posts/:postId/comments/:commentId", target_by([](request& req) {
		const route_state& st = route_of(req);
		return make_response(st.captures.at(":id") + "/" + st.captures.at(":postId") + "/" + st.captures.at(":commentId"));
	}));

	api->param("users/:id", [](std::string_view v) { return !v.empty(); });
	api->param("users/:id/posts/:postId", [](std::string_view v) { return !v.empty(); });
	api->param("users/:id/posts/:postId/comments/:commentId", [](std::string_view v) { return !v.empty(); });

	srv.extends(api);

	if (!srv.listen(endpoint(ip_address::any_v4(), port))) {
		std::fprintf(stderr, "error: can't listen: 0.0.0.0:%u\n", port);
		return 1;
	}

	std::printf("nhttp router-bench server listening on 0.0.0.0:%u\n", port);
	std::fflush(stdout);

	srv.run();
	return 0;
}
