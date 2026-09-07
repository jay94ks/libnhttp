#pragma once

#include "connection.hpp"
#include "connection_h2.hpp"
#include "extension.hpp"
#include "params.hpp"
#include "../async/io_context_pool.hpp"
#include "../async/socket.hpp"
#include "../async/thread_pool.hpp"
#include "../platform/address.hpp"
#include "../platform/socket.hpp"

#ifdef NHTTP_HAVE_TLS
#include "../tls/context.hpp"
#endif

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

#ifdef NHTTP_HAVE_TLS
		/* same as listen(), but every accepted connection on this endpoint
		 * first completes a TLS server handshake (certificate chain + private
		 * key loaded from PEM files) before any HTTP is read from it. requires
		 * building with NHTTP_ENABLE_TLS (the default; needs OpenSSL). */
		bool listen_tls(const platform::endpoint& ep, const std::string& cert_chain_file, const std::string& private_key_file);

		std::optional<platform::endpoint> tls_local_endpoint() const noexcept { return tls_bound_endpoint_; }
#endif

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
		async::task<response> dispatch(request& req);
		async::task<void> run_connection(async::io_context& ctx, std::shared_ptr<io::stream> wire, std::string initial_buffer = std::string());
		async::task<void> run_connection_h2(async::io_context& ctx, std::shared_ptr<io::stream> wire, std::string preface_leftover);

		/* `distribute`: false = keep every accepted connection on `ctx` (the
		 * per-worker SO_REUSEPORT case, kernel already balanced it); true =
		 * round-robin each accepted connection onto a different worker
		 * (the no-SO_REUSEPORT fallback — see bind_listeners in the .cpp). */
		async::detached_task accept_loop(async::io_context& ctx, platform::socket_handle listen_handle, bool distribute);
		async::detached_task handle_connection(async::io_context& ctx, async::async_socket sock);

		/* hands a freshly-accepted, not-yet-touched socket to `target`'s own
		 * thread (io_context::schedule()) before constructing anything bound
		 * to it — required so the fallback accept loop's round-robin never
		 * touches another context's io_registration/reactor state cross-thread. */
		async::detached_task dispatch_accepted(async::io_context& target, platform::socket_handle raw);

		async::io_context& pick_worker_round_robin() noexcept;

#ifdef NHTTP_HAVE_TLS
		async::detached_task accept_loop_tls(async::io_context& ctx, platform::socket_handle listen_handle, std::shared_ptr<tls::tls_context> tls_ctx, bool distribute);
		async::detached_task handle_connection_tls(async::io_context& ctx, async::async_socket sock, std::shared_ptr<tls::tls_context> tls_ctx);
		async::detached_task dispatch_accepted_tls(async::io_context& target, platform::socket_handle raw, std::shared_ptr<tls::tls_context> tls_ctx);
#endif

	private:
		params params_;
		async::io_context_pool pool_;
		async::thread_pool blocking_pool_;
		extension_registry registry_;
		handler_type handler_;

		std::atomic<std::size_t> active_connections_{ 0 };
		std::atomic<std::size_t> next_worker_{ 0 }; // round-robin, only used when SO_REUSEPORT isn't available
		std::optional<platform::endpoint> bound_endpoint_;
#ifdef NHTTP_HAVE_TLS
		std::optional<platform::endpoint> tls_bound_endpoint_;
#endif

		std::mutex run_mutex_;
		std::condition_variable run_cv_;
		std::atomic<bool> stop_requested_{ false };
	};

}
