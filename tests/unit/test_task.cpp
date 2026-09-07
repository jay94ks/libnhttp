#include <catch2/catch_test_macros.hpp>

#include "nhttp/async/task.hpp"
#include "nhttp/async/sync_wait.hpp"

#include <atomic>
#include <stdexcept>

using nhttp::async::detached_task;
using nhttp::async::sync_wait;
using nhttp::async::task;

namespace {

	task<int> get_answer() {
		co_return 42;
	}

	task<int> add_one(int base) {
		int inner = co_await get_answer();
		co_return inner + base;
	}

	task<void> do_nothing() {
		co_return;
	}

	task<int> throws() {
		throw std::runtime_error("boom");
		co_return 1; // unreachable; keeps this a coroutine.
	}

	detached_task run_and_increment(std::atomic<int>& counter) {
		co_await do_nothing();
		++counter;
	}

}

TEST_CASE("task<T> composes and returns its value via sync_wait", "[task]") {
	REQUIRE(sync_wait(add_one(1)) == 43);
}

TEST_CASE("task<void> completes via sync_wait", "[task]") {
	sync_wait(do_nothing());
	SUCCEED();
}

TEST_CASE("task<T> propagates exceptions to the awaiter", "[task]") {
	REQUIRE_THROWS_AS(sync_wait(throws()), std::runtime_error);
}

TEST_CASE("detached_task runs eagerly and frees itself on completion", "[task]") {
	std::atomic<int> counter{ 0 };

	run_and_increment(counter);

	// nothing in the chain (do_nothing/task final_suspend) genuinely suspends on
	// external state, so the whole chain completes synchronously before
	// run_and_increment() returns to the caller.
	REQUIRE(counter.load() == 1);
}
