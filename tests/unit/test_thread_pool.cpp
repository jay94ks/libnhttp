#include <catch2/catch_test_macros.hpp>

#include "nhttp/async/io_context.hpp"
#include "nhttp/async/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace nhttp::async;

namespace {

	void spin_until(std::atomic<bool>& flag) {
		while (!flag.load(std::memory_order_acquire))
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	detached_task run_offloaded_value(io_context& ctx, thread_pool& pool, int& result,
		std::thread::id& ran_on, std::thread::id& resumed_on, std::atomic<bool>& done)
	{
		result = co_await pool.run(ctx, [&ran_on] {
			ran_on = std::this_thread::get_id();
			return 21 * 2;
		});

		resumed_on = std::this_thread::get_id();
		done.store(true, std::memory_order_release);
	}

	detached_task run_offloaded_void(io_context& ctx, thread_pool& pool, std::atomic<bool>& side_effect,
		std::atomic<bool>& done)
	{
		co_await pool.run(ctx, [&side_effect] { side_effect.store(true, std::memory_order_release); });
		done.store(true, std::memory_order_release);
	}

}

TEST_CASE("thread_pool::run executes off-thread and resumes back on the io_context thread", "[async][thread_pool]") {
	io_context ctx;
	thread_pool pool(2);

	int result = 0;
	std::thread::id ran_on;
	std::thread::id resumed_on;
	std::atomic<bool> done{ false };

	run_offloaded_value(ctx, pool, result, ran_on, resumed_on, done);

	std::thread reactor_thread([&ctx] { ctx.run(); });
	const std::thread::id reactor_id = reactor_thread.get_id();
	spin_until(done);

	ctx.stop();
	reactor_thread.join();

	REQUIRE(result == 42);
	REQUIRE(ran_on != reactor_id);
	REQUIRE(resumed_on == reactor_id);
}

TEST_CASE("thread_pool::run supports a void-returning function", "[async][thread_pool]") {
	io_context ctx;
	thread_pool pool(1);

	std::atomic<bool> side_effect{ false };
	std::atomic<bool> done{ false };

	run_offloaded_void(ctx, pool, side_effect, done);

	std::thread reactor_thread([&ctx] { ctx.run(); });
	spin_until(done);

	ctx.stop();
	reactor_thread.join();

	REQUIRE(side_effect.load());
}
