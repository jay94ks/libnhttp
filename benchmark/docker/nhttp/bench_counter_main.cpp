// nhttp side of the "scenario 3" benchmark (see ../../../ReadMe.md's
// Benchmarks section): a single endpoint that reads an integer out of a
// file, increments it, writes it back, and responds with the new value --
// compared against an equivalent PHP script (../php/counter.php) under
// nginx+php-fpm and Apache+mod_php. Deliberately its own minimal main(),
// same rationale as bench_main.cpp (scenario 2's static-file benchmark):
// nothing else competing for CPU, and not wired into CMake.
//
// The file read-modify-write happens on `thread_pool` (never inline on a
// reactor thread, per CLAUDE.md's "reactor thread must never block"
// invariant) and is guarded by a plain std::mutex -- since this whole
// server is one process, that gives the same correctness-under-concurrency
// guarantee counter.php gets from flock() across php-fpm/Apache's multiple
// worker processes, so both sides are paying for the same thing, not just
// raw I/O.
#include "nhttp/server/listener.hpp"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

using namespace nhttp;
using namespace nhttp::server;
using namespace nhttp::platform;

namespace {

	std::mutex counter_file_mutex;

	// synchronous stdio, always run via thread_pool -- never called directly
	// from a reactor-thread coroutine.
	long read_increment_write(const std::string& path) {
		std::lock_guard<std::mutex> lock(counter_file_mutex);

		FILE* fp = std::fopen(path.c_str(), "r+");
		if (!fp)
			fp = std::fopen(path.c_str(), "w+");

		if (!fp)
			return -1;

		char buf[64] = {};
		const std::size_t read = std::fread(buf, 1, sizeof(buf) - 1, fp);
		buf[read] = '\0';
		long n = std::atol(buf) + 1;

		// no ftruncate needed: n only ever grows, so its decimal
		// representation never gets SHORTER than what's already on disk --
		// only equal or longer, which always fully overwrites any stale bytes.
		std::rewind(fp);
		const std::string out = std::to_string(n);
		std::fwrite(out.data(), 1, out.size(), fp);
		std::fflush(fp);
		std::fclose(fp);

		return n;
	}

}

int main(int argc, char** argv) {
	const std::uint16_t port = argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 8080;
	const std::string counter_path = argc > 2 ? argv[2] : "/tmp/nhttp_counter.txt";

	params p;
	listener srv(p);

	srv.set_handler([&srv, counter_path](request& req) -> async::task<response> {
		const long n = co_await srv.blocking_pool().run(*req.io_ctx, [counter_path]() {
			return read_increment_write(counter_path);
		});

		co_return make_response(std::to_string(n));
	});

	if (!srv.listen(endpoint(ip_address::any_v4(), port))) {
		std::fprintf(stderr, "error: can't listen: 0.0.0.0:%u\n", port);
		return 1;
	}

	std::printf("nhttp counter-bench server listening on 0.0.0.0:%u, counter file '%s'\n",
		port, counter_path.c_str());
	std::fflush(stdout);

	srv.run();
	return 0;
}
