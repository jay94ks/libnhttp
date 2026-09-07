#pragma once

#include "io_context.hpp"
#include "task.hpp"

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace nhttp::async {

	/**
	 * class thread_pool.
	 * a fixed-size pool of plain OS threads for work that must block a real
	 * thread (filesystem stat/read, etc.) — anything epoll can't cover. never
	 * used for socket I/O, which always goes through io_context instead.
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

		std::mutex mutex_;
		std::condition_variable cv_;
		std::queue<std::function<void()>> jobs_;
		bool stopping_ = false;
		std::vector<std::thread> workers_;
	};

}
