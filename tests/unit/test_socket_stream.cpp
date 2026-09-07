#include <catch2/catch_test_macros.hpp>

#include "nhttp/io/socket_stream.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace nhttp::async;
using namespace nhttp::platform;
using namespace nhttp::io;

namespace {

	void spin_until(std::atomic<bool>& flag) {
		while (!flag.load(std::memory_order_acquire))
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	detached_task run_server(async_socket listener, std::string& received) {
		async_socket conn = co_await listener.accept();
		socket_stream s(std::move(conn));

		char buf[64] = { 0 };
		const std::size_t n = co_await s.read(buf, sizeof(buf));
		received.assign(buf, n);

		co_await s.write(buf, n);
		co_await s.close();
	}

	detached_task run_client(io_context& ctx, std::uint16_t port, std::string& echoed, std::atomic<bool>& done) {
		socket_handle raw = socket_handle::create(ip_version::v4, transport::tcp);
		async_socket sock(ctx, std::move(raw));

		endpoint ep(ip_address::loopback_v4(), port);
		const bool connected = co_await sock.connect(ep);

		if (connected) {
			socket_stream s(std::move(sock));

			const std::string message = "via-stream";
			co_await s.write(message.data(), message.size());

			char buf[64] = { 0 };
			const std::size_t n = co_await s.read(buf, sizeof(buf));
			echoed.assign(buf, n);
		}

		done.store(true, std::memory_order_release);
	}

}

TEST_CASE("socket_stream exposes an async_socket through the generic stream interface", "[io][socket_stream]") {
	io_context ctx;

	socket_handle listen_handle = socket_handle::create(ip_version::v4, transport::tcp);
	REQUIRE(listen_handle.set_reuse_address(true));

	endpoint bind_ep(ip_address::loopback_v4(), 0);
	REQUIRE(listen_handle.bind(bind_ep));
	REQUIRE(listen_handle.listen(4));

	const std::uint16_t port = listen_handle.local_endpoint()->port();

	std::string received;
	std::string echoed;
	std::atomic<bool> done{ false };

	run_server(async_socket(ctx, std::move(listen_handle)), received);
	run_client(ctx, port, echoed, done);

	std::thread reactor_thread([&ctx] { ctx.run(); });
	spin_until(done);

	ctx.stop();
	reactor_thread.join();

	REQUIRE(received == "via-stream");
	REQUIRE(echoed == "via-stream");
}
