#include <catch2/catch_test_macros.hpp>

#include "nhttp/async/io_context_pool.hpp"
#include "nhttp/async/task.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace nhttp::async;

namespace {

	detached_task tick(io_context& ctx, std::atomic<int>& counter) {
		co_await ctx.sleep_for(std::chrono::milliseconds(1));
		counter.fetch_add(1, std::memory_order_relaxed);
	}

}

TEST_CASE("io_context_pool runs independent contexts concurrently on their own threads", "[async][io_context_pool]") {
	constexpr std::size_t worker_count = 4;
	io_context_pool pool(worker_count);

	REQUIRE(pool.size() == worker_count);

	std::atomic<int> counter{ 0 };

	// spawn one timer-driven coroutine per context BEFORE starting the pool's
	// threads, so the initial timer registration for each context happens
	// single-threaded (see test_io_context.cpp for why this ordering matters).
	for (std::size_t i = 0; i < worker_count; ++i)
		tick(pool.context(i), counter);

	pool.start();

	while (counter.load(std::memory_order_relaxed) < static_cast<int>(worker_count))
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

	pool.stop();

	REQUIRE(counter.load() == static_cast<int>(worker_count));
}

TEST_CASE("io_context_pool::next round-robins across contexts", "[async][io_context_pool]") {
	io_context_pool pool(3);

	io_context& a = pool.next();
	io_context& b = pool.next();
	io_context& c = pool.next();
	io_context& d = pool.next();

	REQUIRE(&a != &b);
	REQUIRE(&b != &c);
	REQUIRE(&a == &d);
}
