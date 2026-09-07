#pragma once

#include "io_context.hpp"
#include "task.hpp"
#include "../platform/socket.hpp"

#include <memory>

namespace nhttp::async {

	/**
	 * class async_socket.
	 * a non-blocking socket bound to one io_context. every I/O operation is a
	 * coroutine that suspends (registering interest with the owning io_context)
	 * instead of blocking, and resumes once the reactor observes readiness.
	 */
	class async_socket {
	public:
		async_socket() noexcept = default;
		async_socket(io_context& ctx, platform::socket_handle handle);
		~async_socket();

		async_socket(async_socket&& other) noexcept;
		async_socket& operator=(async_socket&& other) noexcept;

		async_socket(const async_socket&) = delete;
		async_socket& operator=(const async_socket&) = delete;

	public:
		bool valid() const noexcept { return handle_.valid(); }

		platform::socket_handle& native() noexcept { return handle_; }
		const platform::socket_handle& native() const noexcept { return handle_; }

		io_context& context() const noexcept { return *ctx_; }

		void close();

	public:
		task<std::size_t> read_some(void* buf, std::size_t n);
		task<std::size_t> write_some(const void* buf, std::size_t n);
		task<async_socket> accept();
		task<bool> connect(const platform::endpoint& ep);

		/* the sendfile(2) fast path (see PLAN.md's P1) — same would-block/
		 * retry contract as write_some(), just backed by
		 * platform::socket_handle::send_file() instead of write(). Only call
		 * this when platform::socket_handle::supports_send_file() is true. */
		task<std::size_t> send_file(int in_fd, std::int64_t& offset, std::size_t count);

	private:
		struct read_ready_awaiter {
			async_socket& sock;

			bool await_ready() const noexcept { return false; }
			void await_suspend(std::coroutine_handle<> h) const { sock.ctx_->watch_readable(*sock.reg_, h); }
			void await_resume() const noexcept { }
		};

		struct write_ready_awaiter {
			async_socket& sock;

			bool await_ready() const noexcept { return false; }
			void await_suspend(std::coroutine_handle<> h) const { sock.ctx_->watch_writable(*sock.reg_, h); }
			void await_resume() const noexcept { }
		};

		read_ready_awaiter wait_readable() noexcept { return read_ready_awaiter{ *this }; }
		write_ready_awaiter wait_writable() noexcept { return write_ready_awaiter{ *this }; }

	private:
		io_context* ctx_ = nullptr;
		platform::socket_handle handle_;
		std::unique_ptr<io_registration> reg_;
	};

}
