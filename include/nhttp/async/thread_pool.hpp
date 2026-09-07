#pragma once

#include "detail/mpmc_queue.hpp"
#include "io_context.hpp"
#include "task.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

namespace nhttp::async {

	/**
	 * class thread_pool.
	 * a fixed-size pool of plain OS threads for work that must block a real
	 * thread (filesystem stat/read, etc.) — anything epoll can't cover. never
	 * used for socket I/O, which always goes through io_context instead.
	 *
	 * Job storage (`jobs_`) is a lock-free `detail::mpmc_queue` — push/pop
	 * never take a lock. A plain mutex + condition_variable (`wake_mutex_`/
	 * `wake_cv_`) exists purely for *event propagation*: waking an idle
	 * worker, never for the queue operations themselves. This split matters
	 * (see PLAN.md's P4 and CLAUDE.md's Phase 16 log for the designs tried
	 * before this one): an earlier attempt used a `std::counting_semaphore`
	 * for wake signaling, which requires *every single* dequeue — even by an
	 * already-busy worker immediately picking up the next queued job — to
	 * pay one acquire()/release() pair; measured ~2x slower under sustained
	 * load than the plain mutex+condvar+std::queue this replaced, because
	 * the original design lets a "hot" worker (one that finds a job waiting
	 * the instant it loops back) skip synchronization entirely by just
	 * re-locking its own queue mutex, something a semaphore's per-item
	 * accounting can't do. This design gets both properties at once: a hot
	 * worker's loop is pure lock-free try_pop() with no synchronization
	 * primitive touched at all, and `enqueue()` unconditionally does a plain
	 * mutex-guarded `notify_one()` — no "is anyone actually waiting" gate —
	 * since glibc's `condition_variable::notify_one()` already skips the
	 * underlying futex-wake syscall internally when nothing is waiting, and
	 * measuring an explicit atomic waiter-count gate on top of that showed
	 * no improvement (only one more load/branch per call for a syscall skip
	 * the library already does).
	 */
	class thread_pool {
	public:
		explicit thread_pool(std::size_t worker_count);
		~thread_pool();

		thread_pool(const thread_pool&) = delete;
		thread_pool(thread_pool&&) = delete;

	public:
		/**
		 * runs `func` on a pool thread, then resumes the awaiting coroutine back
		 * on `resume_ctx` (the io_context the caller wants to continue on).
		 */
		template<typename F>
		auto run(io_context& resume_ctx, F func) -> task<std::invoke_result_t<F>> {
			using result_type = std::invoke_result_t<F>;

			struct awaiter {
				thread_pool& pool;
				io_context& ctx;
				F fn;
				std::exception_ptr eptr{};
				std::conditional_t<std::is_void_v<result_type>, char, std::optional<result_type>> storage{};

				bool await_ready() const noexcept { return false; }

				void await_suspend(std::coroutine_handle<> h) {
					pool.enqueue([this, h]() mutable {
						try {
							if constexpr (std::is_void_v<result_type>)
								fn();
							else
								storage.emplace(fn());
						}
						catch (...) {
							eptr = std::current_exception();
						}

						ctx.post(h);
					});
				}

				result_type await_resume() {
					if (eptr)
						std::rethrow_exception(eptr);

					if constexpr (!std::is_void_v<result_type>)
						return std::move(*storage);
				}
			};

			if constexpr (std::is_void_v<result_type>) {
				co_await awaiter{ *this, resume_ctx, std::move(func) };
				co_return;
			}
			else {
				co_return co_await awaiter{ *this, resume_ctx, std::move(func) };
			}
		}

	private:
		void enqueue(std::function<void()> job);
		void worker_loop();

		// generous headroom over any realistic pending-job depth for this
		// codebase's actual usage (bounded by in-flight connections doing
		// filesystem work, not an unbounded external queue) — see
		// mpmc_queue's own doc comment for what happens in the (practically
		// unreachable here) full case.
		static constexpr std::size_t queue_capacity = 8192;

		detail::mpmc_queue<std::function<void()>> jobs_{ queue_capacity };

		// event-propagation only (see this class's doc comment) — never
		// held around a jobs_ push/pop, only around the idle-wait dance in
		// worker_loop() and the notify in enqueue().
		std::mutex wake_mutex_;
		std::condition_variable wake_cv_;

		std::atomic<bool> stopping_{ false };
		std::vector<std::thread> workers_;
	};

}
