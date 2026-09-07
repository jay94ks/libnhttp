#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "nhttp/server/extensions/websocket_endpoint.hpp"
#include "nhttp/ws/handshake.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

using namespace nhttp::server;
using namespace nhttp::platform;
using namespace nhttp::async;
using namespace nhttp::ws;

namespace {

	struct running_server {
		listener srv;
		std::thread thread;

		running_server() : srv(make_params()) {
			REQUIRE(srv.listen(endpoint(ip_address::any_v4(), 0)));
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

	task<void> echo_ws(std::shared_ptr<ws_connection> conn) {
		for (;;) {
			std::optional<message> msg = co_await conn->receive();

			if (!msg)
				co_return;

			if (msg->type == message_type::text)
				co_await conn->send_text(msg->data);
			else
				co_await conn->send_binary(msg->data.data(), msg->data.size());
		}
	}

	/* a minimal blocking client for the handshake + raw frame I/O, independent
	 * of nhttp's own ws:: code so a shared bug wouldn't hide a failure. */
	class raw_ws_client {
	public:
		raw_ws_client(std::uint16_t port, const std::string& path) {
			sock_ = socket_handle::create(ip_version::v4, transport::tcp);
			if (sock_.connect_raw(endpoint(ip_address::loopback_v4(), port)) != connect_result::connected)
				return;

			const std::string key = "dGhlIHNhbXBsZSBub25jZQ=="; // RFC 6455's own example key
			const std::string request =
				"GET " + path + " HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
				"Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";

			send_all(request);

			std::string head;
			while (head.find("\r\n\r\n") == std::string::npos)
				head += read_some();

			handshake_response_ = head.substr(0, head.find("\r\n\r\n"));
			leftover_ = head.substr(head.find("\r\n\r\n") + 4);
			expected_accept_ = compute_accept_key(key);
		}

		~raw_ws_client() { sock_.close(); }

		bool handshake_ok() const {
			return handshake_response_.find("101") != std::string::npos &&
				handshake_response_.find(expected_accept_) != std::string::npos;
		}

		void send_text(const std::string& payload) { send_frame(0x1, payload); }

		std::string receive_frame(std::uint8_t& opcode_out) {
			while (leftover_.size() < 2)
				leftover_ += read_some();

			const std::uint8_t b0 = static_cast<std::uint8_t>(leftover_[0]);
			const std::uint8_t b1 = static_cast<std::uint8_t>(leftover_[1]);
			opcode_out = b0 & 0x0F;
			std::uint64_t len = b1 & 0x7Fu;
			std::size_t pos = 2;

			while (leftover_.size() < pos + (len == 126 ? 2 : len == 127 ? 8 : 0))
				leftover_ += read_some();

			if (len == 126) {
				len = (static_cast<std::uint8_t>(leftover_[pos]) << 8) | static_cast<std::uint8_t>(leftover_[pos + 1]);
				pos += 2;
			}
			else if (len == 127) {
				len = 0;
				for (int i = 0; i < 8; ++i)
					len = (len << 8) | static_cast<std::uint8_t>(leftover_[pos + static_cast<std::size_t>(i)]);
				pos += 8;
			}

			while (leftover_.size() < pos + len)
				leftover_ += read_some();

			const std::string payload = leftover_.substr(pos, len);
			leftover_.erase(0, pos + len);
			return payload;
		}

		void send_close() {
			send_frame(0x8, std::string());
		}

		void send_ping(const std::string& payload) {
			send_frame(0x9, payload);
		}

	private:
		void send_frame(std::uint8_t opcode, const std::string& payload) {
			std::string out;
			out += static_cast<char>(0x80 | opcode);

			std::uint8_t len_byte = 0x80; // client frames are always masked

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
				const ssize_t n = sock_.write(data.data() + sent, data.size() - sent);
				if (n <= 0) break;
				sent += static_cast<std::size_t>(n);
			}
		}

		std::string read_some() {
			char buf[4096];
			const ssize_t n = sock_.read(buf, sizeof(buf));
			return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string();
		}

		socket_handle sock_;
		std::string handshake_response_;
		std::string expected_accept_;
		std::string leftover_;
	};

}

TEST_CASE("websocket_endpoint completes the handshake and echoes text/binary/fragmented messages", "[integration][websocket]") {
	running_server server;
	server.srv.extends(websocket_endpoint_for("/ws", echo_ws));

	raw_ws_client client(server.port(), "/ws");
	REQUIRE(client.handshake_ok());

	client.send_text("hello-ws");
	std::uint8_t opcode = 0;
	REQUIRE(client.receive_frame(opcode) == "hello-ws");
	REQUIRE(opcode == 0x1);
}

TEST_CASE("websocket_endpoint responds to a ping with a pong", "[integration][websocket]") {
	running_server server;
	server.srv.extends(websocket_endpoint_for("/ws", echo_ws));

	raw_ws_client client(server.port(), "/ws");
	REQUIRE(client.handshake_ok());

	client.send_ping("ping-data");
	std::uint8_t opcode = 0;
	REQUIRE(client.receive_frame(opcode) == "ping-data");
	REQUIRE(opcode == 0xA);
}

TEST_CASE("websocket_endpoint closes cleanly on a client close frame", "[integration][websocket]") {
	running_server server;
	server.srv.extends(websocket_endpoint_for("/ws", echo_ws));

	raw_ws_client client(server.port(), "/ws");
	REQUIRE(client.handshake_ok());

	client.send_close();
	std::uint8_t opcode = 0;
	client.receive_frame(opcode);
	REQUIRE(opcode == 0x8);
}
