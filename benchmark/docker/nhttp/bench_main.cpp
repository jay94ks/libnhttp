// Minimal static-file server for the Docker network-stack benchmark (see
// ../../../ReadMe.md's "Docker network-stack benchmark" section). Deliberately
// not examples/nhttpd/main.cpp: that demo binds both loopback_v4() and
// loopback_v6() (see USAGE.md's smoke-test matrix), but a container's peers
// reach it over the bridge network's assigned address, not loopback, and
// typical container networking has no IPv6 stack at all — so this binds
// any_v4() (0.0.0.0) instead, on a single overlay extension, nothing else.
#include "nhttp/server/listener.hpp"
#include "nhttp/server/extensions/overlay.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nhttp;
using namespace nhttp::server;
using namespace nhttp::platform;

int main(int argc, char** argv) {
	const std::string serve_dir = argc > 1 ? argv[1] : ".";
	const std::uint16_t port = argc > 2 ? static_cast<std::uint16_t>(std::atoi(argv[2])) : 8080;

	params p;

	// only override params' own default (4) if the caller explicitly asks —
	// see PLAN.md's P4: with the sendfile(2) fast path (P1) handling static
	// files' actual data transfer, a request only ever touches the blocking
	// pool for two quick hops (stat + open), and this codebase's lock-free
	// thread_pool queue performs *best* at (or near) that small default,
	// not at a large manually-tuned value — over-provisioning workers here
	// now actively hurts throughput via CPU oversubscription/context-switch
	// overhead, the opposite of the old mutex-based pool's tuning advice.
	if (argc > 3)
		p.blocking_pool_size = static_cast<std::size_t>(std::atoi(argv[3]));

	listener srv(p);

	auto static_files = std::make_shared<overlay>(serve_dir, "index.html", srv.blocking_pool(), 0x10000000u);
	srv.extends(static_files);

	if (!srv.listen(endpoint(ip_address::any_v4(), port))) {
		std::fprintf(stderr, "error: can't listen: 0.0.0.0:%u\n", port);
		return 1;
	}

	std::printf("nhttp bench server listening on 0.0.0.0:%u, serving '%s', blocking_pool_size=%zu, io_worker_count=%zu\n",
		port, serve_dir.c_str(), p.blocking_pool_size, p.io_worker_count);
	std::fflush(stdout);

	srv.run();
	return 0;
}
