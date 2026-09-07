#pragma once

#include "task.hpp"

#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>

namespace nhttp::async {

	/**
	 * sync_wait(task) blocks the calling thread until the given task completes,
	 * then returns its result (or re-throws its exception).
	 *
	 * this exists ONLY for tests and for top-level bootstrap code (e.g. blocking
	 * `main()` on the server's run loop) — nothing inside the async core itself
	 * may block a thread waiting for another coroutine; see CLAUDE.md's
	 * concurrency invariant.
	 */

	namespace detail {

		class sync_wait_event {
		public:
			void wait() {
				std::unique_lock lock(mutex_);
				cv_.wait(lock, [this] { return signaled_; });
			}

			void set() {
				// notify_all() while still holding the lock (rather than releasing
				// first) is deliberate: it guarantees this call has fully returned
				// before wait() can re-acquire the lock and return, so a waiter
				// can never destroy this object out from under a still-in-progress
				// notify — found by ThreadSanitizer as a real race in the more
				// "obvious" unlock-then-notify version.
				std::lock_guard<std::mutex> lock(mutex_);
				signaled_ = true;
				cv_.notify_all();
			}

		private:
			std::mutex mutex_;
			std::condition_variable cv_;
			bool signaled_ = false;
		};

		template<typename T>
		detached_task sync_wait_driver(task<T> t, sync_wait_event& ev, std::optional<T>& out, std::exception_ptr& eptr) {
			try {
				out.emplace(co_await std::move(t));
			}
			catch (...) {
				eptr = std::current_exception();
			}

			ev.set();
		}

		inline detached_task sync_wait_driver_void(task<void> t, sync_wait_event& ev, std::exception_ptr& eptr) {
			try {
				co_await std::move(t);
			}
			catch (...) {
				eptr = std::current_exception();
			}

			ev.set();
		}

	}

	template<typename T>
	T sync_wait(task<T> t) {
		detail::sync_wait_event ev;
		std::optional<T> out;
		std::exception_ptr eptr;

		detail::sync_wait_driver(std::move(t), ev, out, eptr);
		ev.wait();

		if (eptr)
			std::rethrow_exception(eptr);

		return std::move(*out);
	}

	inline void sync_wait(task<void> t) {
		detail::sync_wait_event ev;
		std::exception_ptr eptr;

		detail::sync_wait_driver_void(std::move(t), ev, eptr);
		ev.wait();

		if (eptr)
			std::rethrow_exception(eptr);
	}

}
