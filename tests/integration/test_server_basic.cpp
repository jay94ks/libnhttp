#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "nhttp/io/memory_stream.hpp"
#include "../support/raw_http_client.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

using namespace nhttp::server;
using namespace nhttp::platform;
using namespace nhttp::protocol;
using namespace nhttp::async;
using namespace nhttp::io;
using nhttp_test::raw_http_client;

namespace {

	task<response> test_handler(request& req) {
		if (req.path() == "/" && req.method() == http_method::GET())
			co_return make_response("hello world");

		if (req.path() == "/echo") {
			std::string body;
			co_await req.body->read_all(body);
			co_return make_response(std::move(body), mime_types::TEXT_PLAIN);
		}

		if (req.path() == "/unknown-length") {
			auto stream = std::make_shared<memory_stream>(std::vector<std::uint8_t>{ 'x', 'y', 'z' });
			co_return make_response(std::move(stream), mime_types::TEXT_PLAIN, -1);
		}

		co_return make_response(404);
	}

	struct running_server {
		listener srv;
		std::thread thread;

		explicit running_server(ip_version version) : srv(make_params()) {
			srv.set_handler(test_handler);

			const ip_address addr = version == ip_version::v4 ? ip_address::any_v4() : ip_address::any_v6();
			REQUIRE(srv.listen(endpoint(addr, 0)));

			thread = std::thread([this] { srv.run(); });
		}

		~running_server() {
			srv.stop();
			thread.join();
		}

		std::uint16_t port() const { return srv.local_endpoint()->port(); }

	private:
		static params make_params() {
			params p;
			p.io_worker_count = 2;
			p.blocking_pool_size = 1;
			return p;
		}
	};

}

TEST_CASE("listener serves a simple GET request end-to-end", "[integration][server]") {
	running_server server(ip_version::v4);
	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "hello world");
}

TEST_CASE("listener echoes a POST body sized by Content-Length", "[integration][server]") {
	running_server server(ip_version::v4);
	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	const std::string body = "hello from the client";
	const std::string request =
		"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(body.size()) +
		"\r\nConnection: close\r\n\r\n" + body;

	auto resp = client.send(request);

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == body);
}

TEST_CASE("listener decodes a chunked POST request body", "[integration][server]") {
	running_server server(ip_version::v4);
	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	const std::string request =
		"POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
		"4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";

	auto resp = client.send(request);

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "Wikipedia");
}

TEST_CASE("listener sends a chunked response when the handler's body length is unknown", "[integration][server]") {
	running_server server(ip_version::v4);
	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET /unknown-length HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "xyz");
}

TEST_CASE("listener returns 404 for an unmatched path", "[integration][server]") {
	running_server server(ip_version::v4);
	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET /does-not-exist HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

	REQUIRE(resp.status == 404);
}

TEST_CASE("listener keeps a connection alive across multiple requests", "[integration][server]") {
	running_server server(ip_version::v4);
	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto first = client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
	REQUIRE(first.status == 200);
	REQUIRE(first.body == "hello world");

	auto second = client.send("GET /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
	REQUIRE(second.status == 200);
	REQUIRE(second.body.empty());
}

TEST_CASE("listener serves IPv6 loopback identically to IPv4", "[integration][server]") {
	running_server server(ip_version::v6);
	raw_http_client client(ip_version::v6, server.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "hello world");
}
