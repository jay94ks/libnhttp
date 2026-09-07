#pragma once

#include "../platform/reactor.hpp"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace nhttp::async {

	/**
	 * per-socket reactor bookkeeping. lives in a stable heap allocation owned by
	 * whatever wraps the fd (async_socket), so a `ready_event::user_data` stays
	 * valid across moves of the owning object.
	 */
	struct io_registration {
		platform::native_socket_t fd = platform::invalid_native_socket;
		std::coroutine_handle<> read_waiter;
		std::coroutine_handle<> write_waiter;
		bool registered_with_reactor = false;
		bool interest_read = false;
		bool interest_write = false;
	};

	/**
	 * class io_context.
	 * one platform::reactor + a ready-to-resume queue + a timer heap. `run()`
	 * must be called from the single thread that owns this context for its
	 * lifetime; `post()`/`stop()` are the only operations safe to call from
	 * other threads (used by thread_pool to hand a result back to the
	 * originating context).
	 */
	class io_context {
	public:
		io_context();
		~io_context();

		io_context(const io_context&) = delete;
		io_context(io_context&&) = delete;

	public:
		/* runs the event loop until stop() is called. */
		void run();

		/* thread-safe: asks run() to return once it next wakes. */
		void stop() noexcept;

	public:
		/* thread-safe: schedules `h` to resume on this context's run() thread. */
		void post(std::coroutine_handle<> h);

	public:
		/* the following are NOT thread-safe: only call from this context's own thread
		 * (i.e. from a coroutine currently running on it). */
		void watch_readable(io_registration& reg, std::coroutine_handle<> h);
		void watch_writable(io_registration& reg, std::coroutine_handle<> h);

		/* drops any reactor registration for `reg`; call before the fd is closed. */
		void forget(io_registration& reg) noexcept;

	public:
		struct timer_awaiter {
			io_context& ctx;
			std::chrono::steady_clock::time_point deadline;

			bool await_ready() const noexcept { return false; }
			void await_suspend(std::coroutine_handle<> h) { ctx.add_timer(deadline, h); }
			void await_resume() const noexcept { }
		};

		/* not thread-safe: only await this from a coroutine running on this context. */
		timer_awaiter sleep_for(std::chrono::milliseconds duration) {
			return timer_awaiter{ *this, std::chrono::steady_clock::now() + duration };
		}

		struct schedule_awaiter {
			io_context& ctx;

			bool await_ready() const noexcept { return false; }
			void await_suspend(std::coroutine_handle<> h) const { ctx.post(h); }
			void await_resume() const noexcept { }
		};

		/**
		 * thread-safe: suspends the calling coroutine and resumes it on this
		 * context's own run() thread — the general form of the hand-off
		 * `thread_pool::run()` already does internally. Used where a coroutine
		 * needs to move itself onto a specific io_context's thread before
		 * touching that context's io_registration/reactor state (e.g. handing
		 * an accepted connection to a different worker on platforms without
		 * SO_REUSEPORT — see listener.cpp).
		 */
		schedule_awaiter schedule() noexcept { return schedule_awaiter{ *this }; }

	private:
		void update_interest(io_registration& reg) const;
		void add_timer(std::chrono::steady_clock::time_point deadline, std::coroutine_handle<> h);

		void process_ready_events(const platform::ready_event* events, int count);
		void process_timers();
		void drain_posted();
		int compute_timeout_ms() const;

	private:
		std::unique_ptr<platform::reactor> reactor_;
		std::atomic<bool> stopping_{ false };

		std::mutex posted_mutex_;
		std::vector<std::coroutine_handle<>> posted_;

		struct timer_entry {
			std::chrono::steady_clock::time_point deadline;
			std::coroutine_handle<> handle;
		};

		static bool timer_later(const timer_entry& a, const timer_entry& b) noexcept;

		/* min-heap over timer_entry::deadline; only ever touched from the run() thread. */
		std::vector<timer_entry> timers_;
	};

}
