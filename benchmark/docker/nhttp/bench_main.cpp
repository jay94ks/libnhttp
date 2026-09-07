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
	const std::size_t blocking_pool_size = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3])) : 64;

	params p;
	p.blocking_pool_size = blocking_pool_size;

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
