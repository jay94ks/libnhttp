#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "../support/raw_tls_http_client.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace nhttp::server;
using namespace nhttp::platform;
using namespace nhttp::async;
using nhttp_test::raw_tls_http_client;

namespace {

	/* generates a throwaway self-signed certificate via the `openssl` CLI —
	 * fine for test setup code (not something the library itself ever does). */
	struct temp_tls_cert {
		std::string cert_path = "/tmp/nhttp_test_tls_cert.pem";
		std::string key_path = "/tmp/nhttp_test_tls_key.pem";
		bool ok = false;

		temp_tls_cert() {
			std::remove(cert_path.c_str());
			std::remove(key_path.c_str());

			const std::string cmd =
				"openssl req -x509 -newkey rsa:2048 -keyout " + key_path + " -out " + cert_path +
				" -days 1 -nodes -subj /CN=localhost >/dev/null 2>&1";

			ok = (std::system(cmd.c_str()) == 0);
		}

		~temp_tls_cert() {
			std::remove(cert_path.c_str());
			std::remove(key_path.c_str());
		}
	};

	task<response> tls_test_handler(request& req) {
		if (req.path() == "/echo") {
			std::string body;
			co_await req.body->read_all(body);
			co_return make_response(std::move(body));
		}

		co_return make_response("hello over tls");
	}

	struct running_tls_server {
		listener srv;
		std::thread thread;

		explicit running_tls_server(const temp_tls_cert& cert, std::size_t workers = 2) : srv(make_params(workers)) {
			srv.set_handler(tls_test_handler);
			REQUIRE(srv.listen_tls(endpoint(ip_address::any_v4(), 0), cert.cert_path, cert.key_path));
			thread = std::thread([this] { srv.run(); });
		}

		~running_tls_server() {
			srv.stop();
			thread.join();
		}

		std::uint16_t port() const { return srv.tls_local_endpoint()->port(); }

	private:
		static params make_params(std::size_t workers) {
			params p;
			p.io_worker_count = workers;
			p.blocking_pool_size = 1;
			return p;
		}
	};

}

TEST_CASE("listener::listen_tls serves plaintext HTTP wrapped in a real TLS handshake", "[integration][tls]") {
	temp_tls_cert cert;
	REQUIRE(cert.ok);

	running_tls_server server(cert);

	raw_tls_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "hello over tls");
}

TEST_CASE("listener::listen_tls carries a POST body correctly over TLS", "[integration][tls]") {
	temp_tls_cert cert;
	REQUIRE(cert.ok);

	running_tls_server server(cert);

	raw_tls_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	const std::string body = "hello through an encrypted tunnel";
	auto resp = client.send(
		"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(body.size()) +
		"\r\nConnection: close\r\n\r\n" + body);

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == body);
}

TEST_CASE("listener::listen_tls supports multiple requests over one TLS connection (keep-alive)", "[integration][tls]") {
	temp_tls_cert cert;
	REQUIRE(cert.ok);

	running_tls_server server(cert);

	raw_tls_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto first = client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
	REQUIRE(first.status == 200);
	REQUIRE(first.body == "hello over tls");

	auto second = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(second.status == 200);
	REQUIRE(second.body == "hello over tls");
}

// each fresh connection may land on any of the N worker threads via
// SO_REUSEPORT (see CLAUDE.md's architecture-decisions log) — this exercises
// that spread directly, rather than relying on one connection per test case.
TEST_CASE("listener::listen_tls handles many fresh TLS connections spread across workers", "[integration][tls]") {
	temp_tls_cert cert;
	REQUIRE(cert.ok);

	running_tls_server server(cert, 8);

	int ok = 0, fail = 0;

	for (int i = 0; i < 30; ++i) {
		raw_tls_http_client client(ip_version::v4, server.port());

		if (!client.connected()) {
			++fail;
			continue;
		}

		auto resp = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

		if (resp.status == 200 && resp.body == "hello over tls")
			++ok;
		else
			++fail;
	}

	INFO("ok=" << ok << " fail=" << fail);
	REQUIRE(fail == 0);
}
