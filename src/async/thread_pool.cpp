#include "nhttp/async/thread_pool.hpp"

#include <thread>

namespace nhttp::async {

	thread_pool::thread_pool(std::size_t worker_count) {
		if (worker_count == 0)
			worker_count = 1;

		workers_.reserve(worker_count);

		for (std::size_t i = 0; i < worker_count; ++i)
			workers_.emplace_back([this] { worker_loop(); });
	}

	thread_pool::~thread_pool() {
		stopping_.store(true, std::memory_order_release);

		{
			// briefly held so a worker currently between its "recheck the
			// queue" and "actually wait()" steps (see worker_loop()) can't
			// miss this wakeup — the same reason enqueue()'s notify below
			// takes it too. See this class's doc comment for the full
			// event-propagation design.
			std::lock_guard<std::mutex> lock(wake_mutex_);
		}

		wake_cv_.notify_all();

		for (std::thread& t : workers_) {
			if (t.joinable())
				t.join();
		}
	}

	void thread_pool::enqueue(std::function<void()> job) {
		// try_push only fails if queue_capacity's worth of jobs are all
		// still unconsumed at once — practically unreachable for this
		// codebase's real workloads (see queue_capacity's doc comment), but
		// yielding and retrying here (never touching a mutex/condvar) keeps
		// this safe to call from a reactor thread even in that edge case,
		// since progress only requires *some* worker to drain a slot, not
		// this thread acquiring a lock.
		while (!jobs_.try_push(std::move(job)))
			std::this_thread::yield();

		// unconditional lock + notify_one() — no "is anyone waiting" gate.
		// An earlier version tracked that with an atomic counter specifically
		// to skip this under sustained load (see CLAUDE.md's Phase 16 log),
		// but glibc's condition_variable::notify_one() already avoids the
		// underlying futex-wake syscall internally when nothing is waiting,
		// making that counter redundant work — an extra atomic load plus a
		// branch (and one more cache line for the counter itself) on every
		// single enqueue, for a syscall skip the library was already doing.
		// Simpler and at least as fast; see the mutex acquisition's own
		// comment for why *it* still can't be skipped the same way.
		std::lock_guard<std::mutex> lock(wake_mutex_);
		wake_cv_.notify_one();
	}

	void thread_pool::worker_loop() {
		for (;;) {
			std::function<void()> job;

			if (jobs_.try_pop(job)) {
				job();
				continue;
			}

			std::unique_lock<std::mutex> lock(wake_mutex_);

			// re-check *under the lock*: a push (and its notify_one(), which
			// also needs this same lock) could have raced in between the
			// try_pop() above and acquiring this lock here — without this,
			// that notify could arrive in the gap and be lost, since we
			// weren't inside wait() yet to receive it.
			if (jobs_.try_pop(job)) {
				lock.unlock();
				job();
				continue;
			}

			if (stopping_.load(std::memory_order_acquire))
				return;

			wake_cv_.wait(lock);
			// loop back around — try_pop() again at the top, outside the lock.
		}
	}

}
