#include <catch2/catch_test_macros.hpp>

#include "nhttp/platform/socket.hpp"

#include <cstring>

using namespace nhttp::platform;

TEST_CASE("blocking loopback TCP round trip via socket_handle", "[platform][socket]") {
	socket_handle server = socket_handle::create(ip_version::v4, transport::tcp);
	REQUIRE(server.valid());
	REQUIRE(server.set_reuse_address(true));

	endpoint bind_ep(ip_address::loopback_v4(), 0);
	REQUIRE(server.bind(bind_ep));
	REQUIRE(server.listen(4));

	auto local = server.local_endpoint();
	REQUIRE(local.has_value());

	socket_handle client = socket_handle::create(ip_version::v4, transport::tcp);
	REQUIRE(client.valid());

	endpoint connect_ep(ip_address::loopback_v4(), local->port());
	REQUIRE(client.connect_raw(connect_ep) == connect_result::connected);

	sockaddr_storage peer_addr{};
	socklen_t peer_len = 0;
	int accepted_fd = server.accept_raw(peer_addr, peer_len);
	REQUIRE(accepted_fd >= 0);

	socket_handle accepted(accepted_fd);

	const char message[] = "hello, nhttp";
	REQUIRE(client.write(message, sizeof(message)) == static_cast<ssize_t>(sizeof(message)));

	char buffer[64] = { 0 };
	ssize_t n = accepted.read(buffer, sizeof(buffer));
	REQUIRE(n == static_cast<ssize_t>(sizeof(message)));
	REQUIRE(std::memcmp(buffer, message, sizeof(message)) == 0);
}

TEST_CASE("ip_address parses and round-trips v4 and v6 literals", "[platform][address]") {
	auto v4 = ip_address::parse("127.0.0.1");
	REQUIRE(v4.has_value());
	REQUIRE(v4->is_v4());
	REQUIRE(v4->to_string() == "127.0.0.1");

	auto v6 = ip_address::parse("::1");
	REQUIRE(v6.has_value());
	REQUIRE(v6->is_v6());
	REQUIRE(v6->to_string() == "::1");

	REQUIRE_FALSE(ip_address::parse("not-an-address").has_value());
}
