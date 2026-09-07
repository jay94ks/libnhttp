#pragma once

#include "connection.hpp"
#include "extension.hpp"
#include "params.hpp"
#include "../async/io_context_pool.hpp"
#include "../async/socket.hpp"
#include "../async/thread_pool.hpp"
#include "../platform/address.hpp"
#include "../platform/socket.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

namespace nhttp::server {

	/**
	 * class listener.
	 * accepts HTTP/1.1 connections across an io_context_pool. each worker gets
	 * its own SO_REUSEPORT-bound listening socket per registered endpoint, so
	 * the kernel load-balances accepted connections across worker threads — see
	 * CLAUDE.md's architecture decisions. a connection stays on whichever
	 * worker accepted it for its entire lifetime.
	 */
	class listener {
	public:
		explicit listener(params p = params());
		~listener();

		listener(const listener&) = delete;
		listener(listener&&) = delete;

	public:
		/* binds `ep` on every worker context via SO_REUSEPORT. call before run().
		 * if ep's port is 0, the OS-assigned port (learned from the first bind)
		 * is reused for the remaining workers and reported via local_endpoint(). */
		bool listen(const platform::endpoint& ep);

		std::optional<platform::endpoint> local_endpoint() const noexcept { return bound_endpoint_; }

		/* the terminal fallback used when no extension accepts a request. */
		void set_handler(handler_type handler) { handler_ = std::move(handler); }

		/* extensions are tried (priority order) before falling back to the handler. */
		void extends(extension_ptr ext) { registry_.add(std::move(ext)); }

		async::thread_pool& blocking_pool() noexcept { return blocking_pool_; }
		async::io_context_pool& io_pool() noexcept { return pool_; }

		/* blocks the calling thread, running the reactor pool, until stop() is called. */
		void run();

		/* thread-safe: asks run() to return. */
		void stop();

	private:
		async::detached_task accept_loop(async::io_context& ctx, platform::socket_handle listen_handle);
		async::detached_task handle_connection(async::io_context& ctx, async::async_socket sock);

	private:
		params params_;
		async::io_context_pool pool_;
		async::thread_pool blocking_pool_;
		extension_registry registry_;
		handler_type handler_;

		std::atomic<std::size_t> active_connections_{ 0 };
		std::optional<platform::endpoint> bound_endpoint_;

		std::mutex run_mutex_;
		std::condition_variable run_cv_;
		std::atomic<bool> stop_requested_{ false };
	};

}
