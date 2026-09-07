#include <catch2/catch_test_macros.hpp>

#include "nhttp/async/detail/mpmc_queue.hpp"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

using namespace nhttp::async::detail;

TEST_CASE("mpmc_queue preserves FIFO order for a single producer/consumer", "[async][mpmc_queue]") {
	mpmc_queue<int> q(8);

	REQUIRE(q.try_push(1));
	REQUIRE(q.try_push(2));
	REQUIRE(q.try_push(3));

	int out = 0;
	REQUIRE(q.try_pop(out));
	REQUIRE(out == 1);
	REQUIRE(q.try_pop(out));
	REQUIRE(out == 2);
	REQUIRE(q.try_pop(out));
	REQUIRE(out == 3);
}

TEST_CASE("mpmc_queue::try_pop on an empty queue returns false", "[async][mpmc_queue]") {
	mpmc_queue<int> q(8);
	int out = 0;

	REQUIRE_FALSE(q.try_pop(out));
}

TEST_CASE("mpmc_queue::try_push fails once full, then succeeds again after a pop frees a slot", "[async][mpmc_queue]") {
	mpmc_queue<int> q(4);

	REQUIRE(q.try_push(1));
	REQUIRE(q.try_push(2));
	REQUIRE(q.try_push(3));
	REQUIRE(q.try_push(4));
	REQUIRE_FALSE(q.try_push(5)); // full: every slot holds an unconsumed item.

	int out = 0;
	REQUIRE(q.try_pop(out));
	REQUIRE(out == 1);

	REQUIRE(q.try_push(5)); // a slot freed up — this also exercises sequence-number wraparound.

	REQUIRE(q.try_pop(out)); REQUIRE(out == 2);
	REQUIRE(q.try_pop(out)); REQUIRE(out == 3);
	REQUIRE(q.try_pop(out)); REQUIRE(out == 4);
	REQUIRE(q.try_pop(out)); REQUIRE(out == 5);
	REQUIRE_FALSE(q.try_pop(out));
}

TEST_CASE("mpmc_queue::try_push leaves its argument untouched when the queue is full", "[async][mpmc_queue]") {
	// a by-value try_push would consume its argument at the call site even
	// on a failed (queue-full) push, silently losing it on the caller's
	// retry loop — this is exactly the bug thread_pool::enqueue()'s retry
	// depends on not happening; see mpmc_queue.hpp's try_push doc comment.
	//
	// capacity 2 (not 1): capacity 1 is a genuine degenerate case for this
	// algorithm that a real "full" check can't reliably use — see
	// mpmc_queue.hpp's doc comment, which this exact scenario found.
	mpmc_queue<std::string> q(2);

	REQUIRE(q.try_push(std::string("a")));
	REQUIRE(q.try_push(std::string("b")));

	std::string value = "still-here";
	REQUIRE_FALSE(q.try_push(std::move(value)));
	REQUIRE(value == "still-here");
}

TEST_CASE("mpmc_queue delivers every item exactly once under concurrent multi-producer/multi-consumer load", "[async][mpmc_queue]") {
	constexpr int producers = 8;
	constexpr int consumers = 4;
	constexpr int per_producer = 20000;
	constexpr int total = producers * per_producer;

	mpmc_queue<int> q(1024); // deliberately much smaller than `total`, so
	                         // producers must repeatedly hit "full" and
	                         // consumers must actually drain concurrently
	                         // for this test to complete at all.

	std::vector<std::atomic<int>> seen(static_cast<std::size_t>(total));

	for (auto& s : seen)
		s.store(0, std::memory_order_relaxed);

	std::atomic<int> pushed_total{ 0 };
	std::atomic<bool> producers_done{ false };
	std::atomic<int> popped_total{ 0 };
	std::atomic<bool> saw_out_of_range{ false };

	std::vector<std::thread> producer_threads;
	std::vector<std::thread> consumer_threads;

	for (int p = 0; p < producers; ++p) {
		producer_threads.emplace_back([&, p] {
			const int base = p * per_producer;

			for (int i = 0; i < per_producer; ++i) {
				int value = base + i;

				while (!q.try_push(std::move(value)))
					std::this_thread::yield();

				pushed_total.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	for (int c = 0; c < consumers; ++c) {
		consumer_threads.emplace_back([&] {
			for (;;) {
				int value = 0;

				if (q.try_pop(value)) {
					if (value < 0 || value >= total)
						saw_out_of_range.store(true, std::memory_order_relaxed);
					else
						seen[static_cast<std::size_t>(value)].fetch_add(1, std::memory_order_relaxed);

					popped_total.fetch_add(1, std::memory_order_relaxed);
					continue;
				}

				if (producers_done.load(std::memory_order_acquire) && pushed_total.load(std::memory_order_acquire) == popped_total.load(std::memory_order_acquire))
					return;

				std::this_thread::yield();
			}
		});
	}

	for (std::thread& t : producer_threads)
		t.join();

	producers_done.store(true, std::memory_order_release);

	for (std::thread& t : consumer_threads)
		t.join();

	REQUIRE_FALSE(saw_out_of_range.load());
	REQUIRE(popped_total.load() == total);

	for (int i = 0; i < total; ++i)
		REQUIRE(seen[static_cast<std::size_t>(i)].load() == 1);
}
