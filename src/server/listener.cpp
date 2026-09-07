#include "nhttp/server/listener.hpp"
#include "nhttp/io/socket_stream.hpp"

#ifdef NHTTP_HAVE_TLS
#include "nhttp/tls/stream.hpp"
#endif

namespace nhttp::server {

	listener::listener(params p)
		: params_(p), pool_(p.io_worker_count), blocking_pool_(p.blocking_pool_size)
	{
	}

	listener::~listener() {
		stop();
	}

	bool listener::listen(const platform::endpoint& ep) {
		const std::size_t n = pool_.size();
		platform::endpoint actual_ep = ep;

		for (std::size_t i = 0; i < n; ++i) {
			platform::socket_handle handle = platform::socket_handle::create(actual_ep.address().version(), platform::transport::tcp);

			if (!handle.valid())
				return false;

			handle.set_reuse_address(true);
			handle.set_reuse_port(true);

			if (!handle.bind(actual_ep) || !handle.listen(128))
				return false;

			if (i == 0) {
				// if port 0 was requested, learn the OS-assigned port so every
				// other worker's SO_REUSEPORT bind targets the same port.
				if (auto bound = handle.local_endpoint())
					actual_ep = platform::endpoint(actual_ep.address(), bound->port());
			}

			accept_loop(pool_.context(i), std::move(handle));
		}

		bound_endpoint_ = actual_ep;
		return true;
	}

#ifdef NHTTP_HAVE_TLS
	bool listener::listen_tls(const platform::endpoint& ep, const std::string& cert_chain_file, const std::string& private_key_file) {
		std::shared_ptr<tls::tls_context> tls_ctx = tls::tls_context::create_server(cert_chain_file, private_key_file);

		if (!tls_ctx)
			return false;

		const std::size_t n = pool_.size();
		platform::endpoint actual_ep = ep;

		for (std::size_t i = 0; i < n; ++i) {
			platform::socket_handle handle = platform::socket_handle::create(actual_ep.address().version(), platform::transport::tcp);

			if (!handle.valid())
				return false;

			handle.set_reuse_address(true);
			handle.set_reuse_port(true);

			if (!handle.bind(actual_ep) || !handle.listen(128))
				return false;

			if (i == 0) {
				if (auto bound = handle.local_endpoint())
					actual_ep = platform::endpoint(actual_ep.address(), bound->port());
			}

			accept_loop_tls(pool_.context(i), std::move(handle), tls_ctx);
		}

		tls_bound_endpoint_ = actual_ep;
		return true;
	}
#endif

	void listener::run() {
		pool_.start();

		std::unique_lock<std::mutex> lock(run_mutex_);
		run_cv_.wait(lock, [this] { return stop_requested_.load(); });

		pool_.stop();
	}

	void listener::stop() {
		stop_requested_.store(true, std::memory_order_release);
		run_cv_.notify_all();
	}

	async::task<response> listener::dispatch(request& req) {
		std::optional<response> ext_result = co_await registry_.dispatch(req);

		if (ext_result)
			co_return std::move(*ext_result);

		if (handler_)
			co_return co_await handler_(req);

		co_return make_response(501);
	}

	async::task<void> listener::run_connection(async::io_context& ctx, std::shared_ptr<io::stream> wire) {
		connection conn(std::move(wire), params_, ctx, [this](request& req) { return dispatch(req); });
		co_await conn.run();
	}

	async::detached_task listener::accept_loop(async::io_context& ctx, platform::socket_handle listen_handle) {
		async::async_socket listen_sock(ctx, std::move(listen_handle));

		for (;;) {
			async::async_socket client = co_await listen_sock.accept();

			if (active_connections_.load(std::memory_order_relaxed) >= params_.max_connections) {
				client.close();
				continue;
			}

			active_connections_.fetch_add(1, std::memory_order_relaxed);
			handle_connection(ctx, std::move(client));
		}
	}

	async::detached_task listener::handle_connection(async::io_context& ctx, async::async_socket sock) {
		sock.native().set_nodelay(params_.tcp_nodelay);

		if (params_.tcp_keepalive)
			sock.native().set_keepalive(true);

		if (params_.tcp_linger)
			sock.native().set_linger(true, params_.tcp_linger_seconds);

		auto wire = std::make_shared<io::socket_stream>(std::move(sock));

		try {
			co_await run_connection(ctx, std::move(wire));
		}
		catch (...) {
			// a single connection's failure must never take down the listener.
		}

		active_connections_.fetch_sub(1, std::memory_order_relaxed);
	}

#ifdef NHTTP_HAVE_TLS
	async::detached_task listener::accept_loop_tls(async::io_context& ctx, platform::socket_handle listen_handle, std::shared_ptr<tls::tls_context> tls_ctx) {
		async::async_socket listen_sock(ctx, std::move(listen_handle));

		for (;;) {
			async::async_socket client = co_await listen_sock.accept();

			if (active_connections_.load(std::memory_order_relaxed) >= params_.max_connections) {
				client.close();
				continue;
			}

			active_connections_.fetch_add(1, std::memory_order_relaxed);
			handle_connection_tls(ctx, std::move(client), tls_ctx);
		}
	}

	async::detached_task listener::handle_connection_tls(async::io_context& ctx, async::async_socket sock, std::shared_ptr<tls::tls_context> tls_ctx) {
		sock.native().set_nodelay(params_.tcp_nodelay);

		if (params_.tcp_keepalive)
			sock.native().set_keepalive(true);

		if (params_.tcp_linger)
			sock.native().set_linger(true, params_.tcp_linger_seconds);

		auto raw_wire = std::make_shared<io::socket_stream>(std::move(sock));
		auto tls_wire = std::make_shared<tls::tls_stream>(raw_wire, std::move(tls_ctx));

		try {
			if (co_await tls_wire->accept())
				co_await run_connection(ctx, tls_wire);
		}
		catch (...) {
			// a single connection's failure must never take down the listener.
		}

		active_connections_.fetch_sub(1, std::memory_order_relaxed);
	}
#endif

}
