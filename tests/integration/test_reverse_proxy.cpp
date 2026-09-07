#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "nhttp/server/extensions/reverse_proxy.hpp"
#include "nhttp/server/extensions/websocket_endpoint.hpp"
#include "nhttp/ws/handshake.hpp"
#include "../support/raw_http_client.hpp"

#include <cstdint>
#include <thread>

using namespace nhttp::server;
using namespace nhttp::platform;
using namespace nhttp::async;
using namespace nhttp::ws;
using nhttp_test::raw_http_client;

namespace {

	params test_params(std::size_t workers = 2) {
		params p;
		p.io_worker_count = workers;
		p.blocking_pool_size = 1;
		return p;
	}

	/* the fake "upstream" a reverse_proxy in these tests forwards to. */
	struct fake_upstream {
		listener srv;
		std::thread thread;

		explicit fake_upstream(handler_type handler, std::size_t workers = 2) : srv(test_params(workers)) {
			srv.set_handler(std::move(handler));
			REQUIRE(srv.listen(endpoint(ip_address::loopback_v4(), 0)));
			thread = std::thread([this] { srv.run(); });
		}

		~fake_upstream() {
			srv.stop();
			thread.join();
		}

		upstream as_upstream() const {
			return upstream(endpoint(ip_address::loopback_v4(), srv.local_endpoint()->port()));
		}
	};

	struct proxy_server {
		listener srv;
		std::thread thread;

		explicit proxy_server(std::vector<upstream> upstreams, std::size_t workers = 2) : srv(test_params(workers)) {
			srv.extends(reverse_proxy_for("/", std::move(upstreams)));
			REQUIRE(srv.listen(endpoint(ip_address::loopback_v4(), 0)));
			thread = std::thread([this] { srv.run(); });
		}

		~proxy_server() {
			srv.stop();
			thread.join();
		}

		std::uint16_t port() const { return srv.local_endpoint()->port(); }
	};

}

TEST_CASE("reverse_proxy relays a plain GET end-to-end", "[integration][reverse_proxy]") {
	fake_upstream upstream([](request& req) -> task<response> {
		co_return make_response("hello from upstream, path=" + req.path());
	});

	proxy_server proxy({ upstream.as_upstream() });

	raw_http_client client(ip_version::v4, proxy.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET /widgets HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "hello from upstream, path=/widgets");
}

TEST_CASE("reverse_proxy relays a POST body end-to-end", "[integration][reverse_proxy]") {
	fake_upstream upstream([](request& req) -> task<response> {
		std::string body;
		co_await req.body->read_all(body);
		co_return make_response("echoed: " + body);
	});

	proxy_server proxy({ upstream.as_upstream() });

	raw_http_client client(ip_version::v4, proxy.port());
	REQUIRE(client.connected());

	const std::string body = "hello through the proxy";
	auto resp = client.send(
		"POST /submit HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(body.size()) +
		"\r\nConnection: close\r\n\r\n" + body);

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "echoed: hello through the proxy");
}

TEST_CASE("reverse_proxy returns 502 when the upstream is unreachable", "[integration][reverse_proxy]") {
	// bind+listen+immediately close a raw socket directly (no listener/async
	// lifecycle involved) so the port is genuinely refused afterward — not
	// merely "bound but nothing ever accept()s from it", which would hang
	// the proxy's connect() instead of failing it fast.
	socket_handle probe = socket_handle::create(ip_version::v4, transport::tcp);
	REQUIRE(probe.valid());
	REQUIRE(probe.set_reuse_address(true));
	REQUIRE(probe.bind(endpoint(ip_address::loopback_v4(), 0)));
	REQUIRE(probe.listen(1));
	const std::uint16_t dead_port = probe.local_endpoint()->port();
	probe.close();

	upstream dead(endpoint(ip_address::loopback_v4(), dead_port));

	proxy_server proxy({ dead });

	raw_http_client client(ip_version::v4, proxy.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(resp.status == 502);
}

TEST_CASE("reverse_proxy round-robins across multiple upstreams", "[integration][reverse_proxy]") {
	fake_upstream upstream_a([](request&) -> task<response> { co_return make_response("A"); });
	fake_upstream upstream_b([](request&) -> task<response> { co_return make_response("B"); });

	proxy_server proxy({ upstream_a.as_upstream(), upstream_b.as_upstream() });

	int a_count = 0, b_count = 0;

	for (int i = 0; i < 10; ++i) {
		raw_http_client client(ip_version::v4, proxy.port());
		REQUIRE(client.connected());

		auto resp = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
		REQUIRE(resp.status == 200);

		if (resp.body == "A") ++a_count;
		else if (resp.body == "B") ++b_count;
	}

	REQUIRE(a_count == 5);
	REQUIRE(b_count == 5);
}

namespace {

	task<void> echo_ws(std::shared_ptr<ws_connection> conn) {
		for (;;) {
			std::optional<message> msg = co_await conn->receive();

			if (!msg)
				co_return;

			co_await conn->send_text(msg->data);
		}
	}

	/* minimal blocking WS client, independent of nhttp's own ws:: code — see
	 * test_websocket_server.cpp's raw_ws_client, duplicated (not shared) on
	 * purpose so a bug shared between the two wouldn't hide a failure. */
	class raw_ws_client {
	public:
		raw_ws_client(std::uint16_t port, const std::string& path) {
			sock_ = socket_handle::create(ip_version::v4, transport::tcp);

			if (sock_.connect(endpoint(ip_address::loopback_v4(), port)) != connect_result::connected)
				return;

			const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
			const std::string request =
				"GET " + path + " HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
				"Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";

			send_all(request);

			std::string head;
			while (head.find("\r\n\r\n") == std::string::npos)
				head += read_some();

			handshake_response_ = head.substr(0, head.find("\r\n\r\n"));
			leftover_ = head.substr(head.find("\r\n\r\n") + 4);
		}

		~raw_ws_client() { sock_.close(); }

		bool handshake_ok() const { return handshake_response_.find("101") != std::string::npos; }

		void send_text(const std::string& payload) { send_frame(0x1, payload); }

		std::string receive_frame() {
			while (leftover_.size() < 2)
				leftover_ += read_some();

			const std::uint8_t b1 = static_cast<std::uint8_t>(leftover_[1]);
			std::uint64_t len = b1 & 0x7Fu;
			std::size_t pos = 2;

			while (leftover_.size() < pos + (len == 126 ? 2 : 0))
				leftover_ += read_some();

			if (len == 126) {
				len = (static_cast<std::uint8_t>(leftover_[pos]) << 8) | static_cast<std::uint8_t>(leftover_[pos + 1]);
				pos += 2;
			}

			while (leftover_.size() < pos + len)
				leftover_ += read_some();

			const std::string payload = leftover_.substr(pos, len);
			leftover_.erase(0, pos + len);
			return payload;
		}

	private:
		void send_frame(std::uint8_t opcode, const std::string& payload) {
			std::string out;
			out += static_cast<char>(0x80 | opcode);

			std::uint8_t len_byte = 0x80;

			if (payload.size() <= 125) {
				len_byte = static_cast<std::uint8_t>(len_byte | payload.size());
				out += static_cast<char>(len_byte);
			}
			else {
				len_byte |= 126;
				out += static_cast<char>(len_byte);
				out += static_cast<char>((payload.size() >> 8) & 0xFF);
				out += static_cast<char>(payload.size() & 0xFF);
			}

			const std::uint8_t key[4] = { 0x01, 0x02, 0x03, 0x04 };
			for (const std::uint8_t k : key)
				out += static_cast<char>(k);

			std::string masked = payload;

			for (std::size_t i = 0; i < masked.size(); ++i)
				masked[i] = static_cast<char>(static_cast<std::uint8_t>(masked[i]) ^ key[i % 4]);

			out += masked;
			send_all(out);
		}

		void send_all(const std::string& data) {
			std::size_t sent = 0;

			while (sent < data.size()) {
				const std::int64_t n = sock_.write(data.data() + sent, data.size() - sent);
				if (n <= 0) break;
				sent += static_cast<std::size_t>(n);
			}
		}

		std::string read_some() {
			char buf[4096];
			const std::int64_t n = sock_.read(buf, sizeof(buf));
			return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string();
		}

		socket_handle sock_;
		std::string handshake_response_;
		std::string leftover_;
	};

}

TEST_CASE("reverse_proxy passes a WebSocket upgrade through to the upstream", "[integration][reverse_proxy][websocket]") {
	fake_upstream upstream([](request&) -> task<response> { co_return make_response(404); }, 2);
	// register the websocket endpoint on the upstream listener directly (it
	// needs its own extension, not just a plain handler).
	upstream.srv.extends(websocket_endpoint_for("/ws", echo_ws));

	proxy_server proxy({ upstream.as_upstream() });

	raw_ws_client client(proxy.port(), "/ws");
	REQUIRE(client.handshake_ok());

	client.send_text("hello through the proxy");
	REQUIRE(client.receive_frame() == "hello through the proxy");
}
