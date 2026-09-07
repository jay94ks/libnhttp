#include <catch2/catch_test_macros.hpp>

#include "nhttp/ws/connection.hpp"
#include "nhttp/io/memory_stream.hpp"
#include "nhttp/async/sync_wait.hpp"

#include <cstdint>

using namespace nhttp::ws;
using namespace nhttp::io;
using namespace nhttp::async;

namespace {

	// wires reads to one stream and writes to another, so a test can feed input
	// bytes and inspect output bytes independently (a single memory_stream
	// shares one cursor for both directions, which would corrupt a duplex test).
	class duplex_stream final : public stream {
	public:
		duplex_stream(std::shared_ptr<stream> read_from, std::shared_ptr<stream> write_to)
			: read_from_(std::move(read_from)), write_to_(std::move(write_to)) { }

		std::int64_t get_length() const override { return -1; }
		bool can_seek() const noexcept override { return false; }
		task<std::int64_t> seek(std::int64_t, seek_origin) override { co_return -1; }
		task<std::size_t> read(void* buf, std::size_t n) override { return read_from_->read(buf, n); }
		task<std::size_t> write(const void* buf, std::size_t n) override { return write_to_->write(buf, n); }
		task<void> flush() override { co_return; }
		task<void> close() override { co_return; }

	private:
		std::shared_ptr<stream> read_from_;
		std::shared_ptr<stream> write_to_;
	};

	std::string build_frame(bool fin, std::uint8_t opcode, const std::string& payload, bool masked, std::uint32_t mask_key = 0x12345678u) {
		std::string out;
		out += static_cast<char>((fin ? 0x80 : 0x00) | (opcode & 0x0F));

		std::uint8_t len_byte = masked ? 0x80 : 0x00;
		std::string ext;

		if (payload.size() <= 125) {
			len_byte = static_cast<std::uint8_t>(len_byte | payload.size());
		}
		else if (payload.size() <= 0xFFFF) {
			len_byte |= 126;
			ext += static_cast<char>((payload.size() >> 8) & 0xFF);
			ext += static_cast<char>(payload.size() & 0xFF);
		}
		else {
			len_byte |= 127;
			for (int shift = 56; shift >= 0; shift -= 8)
				ext += static_cast<char>((static_cast<std::uint64_t>(payload.size()) >> shift) & 0xFF);
		}

		out += static_cast<char>(len_byte);
		out += ext;

		std::string data = payload;

		if (masked) {
			const std::uint8_t key[4] = {
				static_cast<std::uint8_t>((mask_key >> 24) & 0xFF), static_cast<std::uint8_t>((mask_key >> 16) & 0xFF),
				static_cast<std::uint8_t>((mask_key >> 8) & 0xFF), static_cast<std::uint8_t>(mask_key & 0xFF)
			};

			for (const std::uint8_t k : key)
				out += static_cast<char>(k);

			for (std::size_t i = 0; i < data.size(); ++i)
				data[i] = static_cast<char>(static_cast<std::uint8_t>(data[i]) ^ key[i % 4]);
		}

		out += data;
		return out;
	}

	struct parsed_frame {
		bool fin;
		std::uint8_t opcode;
		std::string payload;
	};

	parsed_frame parse_unmasked_frame(const std::string& bytes, std::size_t& offset) {
		const std::uint8_t b0 = static_cast<std::uint8_t>(bytes[offset]);
		const std::uint8_t b1 = static_cast<std::uint8_t>(bytes[offset + 1]);

		const bool fin = (b0 & 0x80) != 0;
		const std::uint8_t opcode = b0 & 0x0F;
		std::uint64_t len = b1 & 0x7Fu;
		std::size_t pos = offset + 2;

		if (len == 126) {
			len = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[pos])) << 8) |
				static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[pos + 1]));
			pos += 2;
		}
		else if (len == 127) {
			len = 0;
			for (int i = 0; i < 8; ++i)
				len = (len << 8) | static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[pos + static_cast<std::size_t>(i)]));
			pos += 8;
		}

		const std::string payload = bytes.substr(pos, len);
		offset = pos + static_cast<std::size_t>(len);

		return parsed_frame{ fin, opcode, payload };
	}

	std::shared_ptr<memory_stream> bytes_of(const std::string& s) {
		return std::make_shared<memory_stream>(std::vector<std::uint8_t>(s.begin(), s.end()));
	}

}

TEST_CASE("ws_connection decodes a single masked text frame from the client", "[ws][connection]") {
	auto input = bytes_of(build_frame(true, 0x1, "hello", true));
	auto output = std::make_shared<memory_stream>();
	ws_connection conn(std::make_shared<duplex_stream>(input, output));

	auto msg = sync_wait(conn.receive());

	REQUIRE(msg.has_value());
	REQUIRE(msg->type == message_type::text);
	REQUIRE(msg->data == "hello");
}

TEST_CASE("ws_connection reassembles a fragmented message across continuation frames", "[ws][connection]") {
	std::string wire = build_frame(false, 0x1, "hello ", true, 0x11223344u);
	wire += build_frame(true, 0x0, "world", true, 0xAABBCCDDu);

	auto input = bytes_of(wire);
	auto output = std::make_shared<memory_stream>();
	ws_connection conn(std::make_shared<duplex_stream>(input, output));

	auto msg = sync_wait(conn.receive());

	REQUIRE(msg.has_value());
	REQUIRE(msg->type == message_type::text);
	REQUIRE(msg->data == "hello world");
}

TEST_CASE("ws_connection::send_text writes an unmasked text frame", "[ws][connection]") {
	auto input = std::make_shared<memory_stream>();
	auto output = std::make_shared<memory_stream>();
	ws_connection conn(std::make_shared<duplex_stream>(input, output));

	sync_wait(conn.send_text("hi there"));

	std::size_t offset = 0;
	const parsed_frame f = parse_unmasked_frame(std::string(output->data().begin(), output->data().end()), offset);

	REQUIRE(f.fin);
	REQUIRE(f.opcode == 0x1);
	REQUIRE(f.payload == "hi there");
}

TEST_CASE("ws_connection auto-responds to a ping with a pong and still delivers the next message", "[ws][connection]") {
	std::string wire = build_frame(true, 0x9, "ping-payload", true); // ping
	wire += build_frame(true, 0x1, "after-ping", true);              // text

	auto input = bytes_of(wire);
	auto output = std::make_shared<memory_stream>();
	ws_connection conn(std::make_shared<duplex_stream>(input, output));

	auto msg = sync_wait(conn.receive());

	REQUIRE(msg.has_value());
	REQUIRE(msg->data == "after-ping");

	std::size_t offset = 0;
	const std::string out_bytes(output->data().begin(), output->data().end());
	const parsed_frame pong = parse_unmasked_frame(out_bytes, offset);

	REQUIRE(pong.opcode == 0xA);
	REQUIRE(pong.payload == "ping-payload");
}

TEST_CASE("ws_connection handles a close frame by echoing close and marking itself closed", "[ws][connection]") {
	auto input = bytes_of(build_frame(true, 0x8, "", true));
	auto output = std::make_shared<memory_stream>();
	ws_connection conn(std::make_shared<duplex_stream>(input, output));

	auto msg = sync_wait(conn.receive());

	REQUIRE_FALSE(msg.has_value());
	REQUIRE(conn.is_closed());

	std::size_t offset = 0;
	const parsed_frame closed = parse_unmasked_frame(std::string(output->data().begin(), output->data().end()), offset);
	REQUIRE(closed.opcode == 0x8);
}

TEST_CASE("ws_connection handles a payload requiring the 16-bit extended length", "[ws][connection]") {
	const std::string big_payload(300, 'z');
	auto input = bytes_of(build_frame(true, 0x2, big_payload, true));
	auto output = std::make_shared<memory_stream>();
	ws_connection conn(std::make_shared<duplex_stream>(input, output));

	auto msg = sync_wait(conn.receive());

	REQUIRE(msg.has_value());
	REQUIRE(msg->type == message_type::binary);
	REQUIRE(msg->data == big_payload);
}
