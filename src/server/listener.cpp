#include "nhttp/server/listener.hpp"
#include "nhttp/io/socket_stream.hpp"

#ifdef NHTTP_HAVE_TLS
#include "nhttp/tls/stream.hpp"
#endif

#if !defined(_WIN32)
#include <csignal>
#endif

namespace nhttp::server {

	listener::listener(params p)
		: params_(p), pool_(p.io_worker_count), blocking_pool_(p.blocking_pool_size)
	{
#if !defined(_WIN32)
		// a write() to a socket the peer has already reset/closed raises
		// SIGPIPE, whose default disposition is to terminate the *entire*
		// process instantly (no core, nothing for a debugger/sanitizer to
		// catch) — found via a sustained wrk benchmark: any client that resets
		// a connection mid-response (routine under load, and guaranteed when
		// wrk tears down its connection pool at a run's end) killed the whole
		// server. Ignoring it here makes those writes fail normally with
		// EPIPE instead, which the existing socket-error handling already
		// treats as an ordinary closed connection. Windows has no SIGPIPE for
		// socket writes (it reports WSAECONNRESET/WSAECONNABORTED instead), so
		// this is POSIX-only.
		std::signal(SIGPIPE, SIG_IGN);
#endif
	}

	listener::~listener() {
		stop();
	}

	bool listener::listen(const platform::endpoint& ep) {
		const std::size_t n = pool_.size();
		platform::endpoint actual_ep = ep;

		// SO_REUSEPORT doesn't exist on Windows — set_reuse_port() reports that
		// (see platform::socket_handle::set_reuse_port's doc comment) rather
		// than silently pretending it worked, so this is a real capability
		// check, not a guess. Where it's unavailable, bind exactly one
		// listening socket and round-robin accepted connections across workers
		// explicitly (accept_loop's `distribute` flag) instead of relying on
		// the kernel to spread them across N independently-bound sockets.
		platform::socket_handle probe = platform::socket_handle::create(ep.address().version(), platform::transport::tcp);

		if (!probe.valid())
			return false;

		const bool have_reuseport = probe.set_reuse_port(true);
		probe.close();

		if (!have_reuseport) {
			platform::socket_handle handle = platform::socket_handle::create(actual_ep.address().version(), platform::transport::tcp);

			if (!handle.valid())
				return false;

			handle.set_reuse_address(true);

			if (!handle.bind(actual_ep) || !handle.listen(128))
				return false;

			if (auto bound = handle.local_endpoint())
				actual_ep = platform::endpoint(actual_ep.address(), bound->port());

			accept_loop(pool_.context(0), std::move(handle), /* distribute = */ true);
			bound_endpoint_ = actual_ep;
			return true;
		}

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

			accept_loop(pool_.context(i), std::move(handle), /* distribute = */ false);
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

		platform::socket_handle probe = platform::socket_handle::create(ep.address().version(), platform::transport::tcp);

		if (!probe.valid())
			return false;

		const bool have_reuseport = probe.set_reuse_port(true);
		probe.close();

		if (!have_reuseport) {
			platform::socket_handle handle = platform::socket_handle::create(actual_ep.address().version(), platform::transport::tcp);

			if (!handle.valid())
				return false;

			handle.set_reuse_address(true);

			if (!handle.bind(actual_ep) || !handle.listen(128))
				return false;

			if (auto bound = handle.local_endpoint())
				actual_ep = platform::endpoint(actual_ep.address(), bound->port());

			accept_loop_tls(pool_.context(0), std::move(handle), tls_ctx, /* distribute = */ true);
			tls_bound_endpoint_ = actual_ep;
			return true;
		}

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

			accept_loop_tls(pool_.context(i), std::move(handle), tls_ctx, /* distribute = */ false);
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

	async::task<void> listener::run_connection(async::io_context& ctx, std::shared_ptr<io::stream> wire, std::string initial_buffer) {
		connection conn(std::move(wire), params_, ctx, [this](request& req) { return dispatch(req); }, std::move(initial_buffer));
		co_await conn.run();
	}

	async::task<void> listener::run_connection_h2(async::io_context& ctx, std::shared_ptr<io::stream> wire, std::string preface_leftover) {
		connection_h2 conn(std::move(wire), params_, ctx, [this](request& req) { return dispatch(req); }, std::move(preface_leftover));
		co_await conn.run();
	}

	async::io_context& listener::pick_worker_round_robin() noexcept {
		const std::size_t i = next_worker_.fetch_add(1, std::memory_order_relaxed) % pool_.size();
		return pool_.context(i);
	}

	async::detached_task listener::dispatch_accepted(async::io_context& target, platform::socket_handle raw) {
		co_await target.schedule(); // now running on target's own thread.

		async::async_socket sock(target, std::move(raw));

		if (active_connections_.load(std::memory_order_relaxed) >= params_.max_connections) {
			sock.close();
			co_return;
		}

		active_connections_.fetch_add(1, std::memory_order_relaxed);
		handle_connection(target, std::move(sock));
	}

	async::detached_task listener::accept_loop(async::io_context& ctx, platform::socket_handle listen_handle, bool distribute) {
		async::async_socket listen_sock(ctx, std::move(listen_handle));

		for (;;) {
			async::async_socket client = co_await listen_sock.accept();

			if (distribute) {
				// hand off before touching active_connections_/max_connections
				// on the target thread — dispatch_accepted does that check
				// itself once it's actually running there.
				dispatch_accepted(pick_worker_round_robin(), platform::socket_handle(client.native().release()));
				continue;
			}

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
			// peek 4 bytes to distinguish an HTTP/2 prior-knowledge client
			// (whose connection preface starts "PRI ", a request-line shape
			// no real HTTP/1.1 method ever produces — RFC 9113 §3.4 chose it
			// deliberately for exactly this reason) from plain HTTP/1.1.
			// io::stream has no non-destructive peek, so these bytes are read
			// for real and hex-identically replayed as whichever driver's
			// initial buffer — a small, permanent 4-byte read on every
			// plaintext connection, not just h2 ones.
			std::string peeked;

			while (peeked.size() < 4) {
				char buf[4];
				const std::size_t n = co_await wire->read(buf, 4 - peeked.size());

				if (n == 0)
					break; // connection closed before enough bytes arrived — let run_connection's own parser report it.

				peeked.append(buf, n);
			}

			if (peeked == "PRI ")
				co_await run_connection_h2(ctx, wire, std::move(peeked));
			else
				co_await run_connection(ctx, wire, std::move(peeked));
		}
		catch (...) {
			// a single connection's failure must never take down the listener.
		}

		active_connections_.fetch_sub(1, std::memory_order_relaxed);
	}

#ifdef NHTTP_HAVE_TLS
	async::detached_task listener::dispatch_accepted_tls(async::io_context& target, platform::socket_handle raw, std::shared_ptr<tls::tls_context> tls_ctx) {
		co_await target.schedule(); // now running on target's own thread.

		async::async_socket sock(target, std::move(raw));

		if (active_connections_.load(std::memory_order_relaxed) >= params_.max_connections) {
			sock.close();
			co_return;
		}

		active_connections_.fetch_add(1, std::memory_order_relaxed);
		handle_connection_tls(target, std::move(sock), std::move(tls_ctx));
	}

	async::detached_task listener::accept_loop_tls(async::io_context& ctx, platform::socket_handle listen_handle, std::shared_ptr<tls::tls_context> tls_ctx, bool distribute) {
		async::async_socket listen_sock(ctx, std::move(listen_handle));

		for (;;) {
			async::async_socket client = co_await listen_sock.accept();

			if (distribute) {
				dispatch_accepted_tls(pick_worker_round_robin(), platform::socket_handle(client.native().release()), tls_ctx);
				continue;
			}

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
