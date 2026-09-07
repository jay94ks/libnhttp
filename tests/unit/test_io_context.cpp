#include <catch2/catch_test_macros.hpp>

#include "nhttp/async/io_context.hpp"
#include "nhttp/async/socket.hpp"
#include "nhttp/async/sync_wait.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace nhttp::async;
using namespace nhttp::platform;

// NOTE on thread-safety in these tests: an async_socket's io_registration (and an
// io_context's timer heap) are only safe to touch from the single thread that will
// run that io_context's run() loop — see CLAUDE.md's concurrency invariant. So every
// coroutine chain below is *spawned* (its first suspension point reached) before the
// reactor thread starts, which means that first registration happens single-threaded
// with nothing else touching the io_context yet. After that point, every subsequent
// resume/re-register for that chain happens from inside run()'s own dispatch, i.e. on
// the reactor thread — never crossing threads again. Only io_context::post()/stop()
// (and plain std::atomic flags) are safe to touch from a foreign thread after the
// reactor has started; that's exactly what the "post" test below exercises.

namespace {

	detached_task run_echo_server(async_socket listener, std::string& received) {
		async_socket conn = co_await listener.accept();

		char buf[64] = { 0 };
		std::size_t n = co_await conn.read_some(buf, sizeof(buf));
		received.assign(buf, n);

		std::size_t written = 0;
		while (written < n)
			written += co_await conn.write_some(buf + written, n - written);
	}

	detached_task run_echo_client(io_context& ctx, std::uint16_t port, std::string& echoed, std::atomic<bool>& done) {
		socket_handle raw = socket_handle::create(ip_version::v4, transport::tcp);
		async_socket client(ctx, std::move(raw));

		endpoint ep(ip_address::loopback_v4(), port);
		const bool connected = co_await client.connect(ep);

		if (connected) {
			const char message[] = "ping-nhttp";
			constexpr std::size_t message_len = sizeof(message) - 1; // exclude the trailing '\0'
			std::size_t written = 0;
			while (written < message_len)
				written += co_await client.write_some(message + written, message_len - written);

			char buf[64] = { 0 };
			const std::size_t n = co_await client.read_some(buf, sizeof(buf));
			echoed.assign(buf, n);
		}

		done.store(true, std::memory_order_release);
	}

	void spin_until(std::atomic<bool>& flag) {
		while (!flag.load(std::memory_order_acquire))
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	detached_task measure_sleep(io_context& ctx, std::chrono::milliseconds& out, std::atomic<bool>& done) {
		const auto start = std::chrono::steady_clock::now();
		co_await ctx.sleep_for(std::chrono::milliseconds(30));
		out = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
		done.store(true, std::memory_order_release);
	}

}

TEST_CASE("async_socket accept/connect/read/write round trip through a real io_context", "[async][io_context]") {
	io_context ctx;

	socket_handle listen_handle = socket_handle::create(ip_version::v4, transport::tcp);
	REQUIRE(listen_handle.valid());
	REQUIRE(listen_handle.set_reuse_address(true));

	endpoint bind_ep(ip_address::loopback_v4(), 0);
	REQUIRE(listen_handle.bind(bind_ep));
	REQUIRE(listen_handle.listen(8));

	auto local = listen_handle.local_endpoint();
	REQUIRE(local.has_value());
	const std::uint16_t port = local->port();

	std::string received;
	std::string echoed;
	std::atomic<bool> client_done{ false };

	// spawn both sides (reaching their first suspension point) before the reactor
	// thread exists, so the initial epoll registration is single-threaded.
	run_echo_server(async_socket(ctx, std::move(listen_handle)), received);
	run_echo_client(ctx, port, echoed, client_done);

	std::thread reactor_thread([&ctx] { ctx.run(); });

	spin_until(client_done);

	ctx.stop();
	reactor_thread.join();

	REQUIRE(received == "ping-nhttp");
	REQUIRE(echoed == "ping-nhttp");
}

TEST_CASE("io_context::sleep_for resumes after roughly the requested delay", "[async][io_context]") {
	io_context ctx;

	std::chrono::milliseconds elapsed{ 0 };
	std::atomic<bool> done{ false };

	measure_sleep(ctx, elapsed, done);

	std::thread reactor_thread([&ctx] { ctx.run(); });

	spin_until(done);

	ctx.stop();
	reactor_thread.join();

	REQUIRE(elapsed.count() >= 25);
}

TEST_CASE("io_context::post resumes a coroutine on the reactor thread from another thread", "[async][io_context]") {
	io_context ctx;
	std::thread reactor_thread([&ctx] { ctx.run(); });

	std::atomic<std::thread::id> resumed_on{};
	const std::thread::id reactor_id = reactor_thread.get_id();

	struct post_awaiter {
		io_context& ctx;
		bool await_ready() const noexcept { return false; }
		void await_suspend(std::coroutine_handle<> h) const {
			std::thread([this, h] { ctx.post(h); }).detach();
		}
		void await_resume() const noexcept { }
	};

	auto record = [&]() -> task<void> {
		co_await post_awaiter{ ctx };
		resumed_on.store(std::this_thread::get_id());
	};

	sync_wait(record());

	ctx.stop();
	reactor_thread.join();

	REQUIRE(resumed_on.load() == reactor_id);
}
