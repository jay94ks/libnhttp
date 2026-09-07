#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace nhttp::async {

	template<typename T = void>
	class task;

	namespace detail {

		/* shared by task_promise<T> and task_promise<void>. */
		struct task_promise_base {
			std::coroutine_handle<> continuation;

			struct final_awaiter {
				bool await_ready() const noexcept { return false; }

				template<typename Promise>
				std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
					if (std::coroutine_handle<> c = h.promise().continuation)
						return c;

					return std::noop_coroutine();
				}

				void await_resume() const noexcept { }
			};

			std::suspend_always initial_suspend() noexcept { return {}; }
			final_awaiter final_suspend() noexcept { return {}; }
		};

		template<typename T>
		struct task_promise final : task_promise_base {
			task<T> get_return_object() noexcept;

			void return_value(T value) { result_.emplace(std::move(value)); }
			void unhandled_exception() noexcept { exception_ = std::current_exception(); }

			/* moves the result out; a task is single-await, so this is only ever called once. */
			T result() {
				if (exception_)
					std::rethrow_exception(exception_);

				return std::move(*result_);
			}

		private:
			std::optional<T> result_;
			std::exception_ptr exception_;
		};

		template<>
		struct task_promise<void> final : task_promise_base {
			task<void> get_return_object() noexcept;

			void return_void() noexcept { }
			void unhandled_exception() noexcept { exception_ = std::current_exception(); }

			void result() {
				if (exception_)
					std::rethrow_exception(exception_);
			}

		private:
			std::exception_ptr exception_;
		};

	}

	/**
	 * class task<T>.
	 * a lazily-started, single-owner, single-await coroutine task.
	 * the coroutine body doesn't run until the task is co_await-ed (or manually resumed).
	 */
	template<typename T>
	class [[nodiscard]] task {
	public:
		using promise_type = detail::task_promise<T>;

		task() noexcept : handle_(nullptr) { }
		explicit task(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) { }

		task(task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) { }
		task(const task&) = delete;

		task& operator=(task&& other) noexcept {
			if (this != &other) {
				if (handle_)
					handle_.destroy();

				handle_ = std::exchange(other.handle_, nullptr);
			}

			return *this;
		}

		task& operator=(const task&) = delete;

		~task() {
			if (handle_)
				handle_.destroy();
		}

	public:
		/* true if this task was never started or already ran to completion. */
		bool done() const noexcept { return !handle_ || handle_.done(); }
		bool valid() const noexcept { return handle_ != nullptr; }

	public:
		struct awaiter {
			std::coroutine_handle<promise_type> handle;

			bool await_ready() const noexcept { return !handle || handle.done(); }

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
				handle.promise().continuation = awaiting;
				return handle;
			}

			decltype(auto) await_resume() {
				return handle.promise().result();
			}
		};

		/* deliberately does NOT null out handle_: the callee's coroutine frame
		 * finishes at final_suspend (suspended, not destroyed — see
		 * task_promise_base::final_suspend) and stays alive until *this* task
		 * object's destructor runs. For the common `co_await foo()` form, `foo()`
		 * is a prvalue temporary whose lifetime extends to the end of the full
		 * expression (ordinary C++ temporary-lifetime rules), so its destructor
		 * — and the frame-freeing `handle_.destroy()` it calls — runs right after
		 * await_resume() returns the result. Nulling handle_ here (the original,
		 * now-fixed bug) orphaned that frame: nothing else ever destroys it, so
		 * every single co_await leaked its callee's coroutine frame permanently
		 * — found via a sustained-load benchmark showing ~24KB/request growth
		 * serving static files (each request chains dozens of task<T> awaits),
		 * confirmed with a standalone repro (2,000,000 awaited no-op tasks leaked
		 * ~125MB). The awaiter below only borrows the handle to drive
		 * await_suspend/await_resume; it never owns or destroys it. */
		awaiter operator co_await() && noexcept { return awaiter{ handle_ }; }

	private:
		std::coroutine_handle<promise_type> handle_;
	};

	template<typename T>
	inline task<T> detail::task_promise<T>::get_return_object() noexcept {
		return task<T>(std::coroutine_handle<task_promise<T>>::from_promise(*this));
	}

	inline task<void> detail::task_promise<void>::get_return_object() noexcept {
		return task<void>(std::coroutine_handle<task_promise<void>>::from_promise(*this));
	}

	/**
	 * class detached_task.
	 * a fire-and-forget coroutine: starts running immediately when called, and
	 * frees its own coroutine frame when it completes since nothing else owns it.
	 * used to spawn independent top-level work (e.g. one per accepted connection)
	 * from a context that isn't itself a coroutine awaiting the result.
	 */
	class detached_task {
	public:
		struct promise_type {
			detached_task get_return_object() noexcept { return {}; }
			std::suspend_never initial_suspend() noexcept { return {}; }

			struct final_awaiter {
				bool await_ready() const noexcept { return false; }
				void await_suspend(std::coroutine_handle<promise_type> h) noexcept { h.destroy(); }
				void await_resume() const noexcept { }
			};

			final_awaiter final_suspend() noexcept { return {}; }
			void return_void() noexcept { }

			/* a detached task has nobody to propagate exceptions to. */
			[[noreturn]] void unhandled_exception() { std::terminate(); }
		};
	};

}
