#include "nhttp/ws/connection.hpp"

namespace nhttp::ws {

	namespace {
		constexpr std::uint8_t opcode_continuation = 0x0;
		constexpr std::uint8_t opcode_text = 0x1;
		constexpr std::uint8_t opcode_binary = 0x2;
		constexpr std::uint8_t opcode_close = 0x8;
		constexpr std::uint8_t opcode_ping = 0x9;
		constexpr std::uint8_t opcode_pong = 0xA;
	}

	ws_connection::ws_connection(std::shared_ptr<io::stream> wire, std::string initial_buffer)
		: wire_(std::move(wire)), buffer_(std::move(initial_buffer))
	{
	}

	async::task<bool> ws_connection::fill_more() {
		if (eof_)
			co_return false;

		char chunk[4096];
		const std::size_t n = co_await wire_->read(chunk, sizeof(chunk));

		if (n == 0) {
			eof_ = true;
			co_return false;
		}

		buffer_.append(chunk, n);
		co_return true;
	}

	async::task<bool> ws_connection::read_exact(std::string& out, std::size_t n) {
		while (buffer_.size() < n) {
			if (!co_await fill_more())
				co_return false;
		}

		out.assign(buffer_, 0, n);
		buffer_.erase(0, n);
		co_return true;
	}

	async::task<std::optional<ws_connection::raw_frame>> ws_connection::read_frame() {
		std::string head;

		if (!co_await read_exact(head, 2))
			co_return std::nullopt;

		const std::uint8_t b0 = static_cast<std::uint8_t>(head[0]);
		const std::uint8_t b1 = static_cast<std::uint8_t>(head[1]);

		const bool fin = (b0 & 0x80) != 0;
		const std::uint8_t opcode = b0 & 0x0F;
		const bool masked = (b1 & 0x80) != 0;
		std::uint64_t length = b1 & 0x7Fu;

		if (length == 126) {
			std::string ext;

			if (!co_await read_exact(ext, 2))
				co_return std::nullopt;

			length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(ext[0])) << 8) |
				static_cast<std::uint64_t>(static_cast<std::uint8_t>(ext[1]));
		}
		else if (length == 127) {
			std::string ext;

			if (!co_await read_exact(ext, 8))
				co_return std::nullopt;

			length = 0;

			for (int i = 0; i < 8; ++i)
				length = (length << 8) | static_cast<std::uint64_t>(static_cast<std::uint8_t>(ext[static_cast<std::size_t>(i)]));
		}

		std::uint8_t key[4] = { 0, 0, 0, 0 };

		if (masked) {
			std::string keystr;

			if (!co_await read_exact(keystr, 4))
				co_return std::nullopt;

			for (int i = 0; i < 4; ++i)
				key[i] = static_cast<std::uint8_t>(keystr[static_cast<std::size_t>(i)]);
		}

		std::string payload;

		if (length > 0) {
			if (!co_await read_exact(payload, static_cast<std::size_t>(length)))
				co_return std::nullopt;

			if (masked) {
				for (std::size_t i = 0; i < payload.size(); ++i)
					payload[i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ key[i % 4]);
			}
		}

		co_return raw_frame{ fin, opcode, std::move(payload) };
	}

	async::task<void> ws_connection::write_all(const void* data, std::size_t n) {
		const char* p = static_cast<const char*>(data);
		std::size_t written = 0;

		while (written < n)
			written += co_await wire_->write(p + written, n - written);
	}

	async::task<void> ws_connection::write_frame(bool fin, std::uint8_t opcode, const void* data, std::size_t n) {
		std::string header;
		header += static_cast<char>((fin ? 0x80 : 0x00) | (opcode & 0x0F));

		if (n <= 125) {
			header += static_cast<char>(n);
		}
		else if (n <= 0xFFFF) {
			header += static_cast<char>(126);
			header += static_cast<char>((n >> 8) & 0xFF);
			header += static_cast<char>(n & 0xFF);
		}
		else {
			header += static_cast<char>(127);

			for (int shift = 56; shift >= 0; shift -= 8)
				header += static_cast<char>((static_cast<std::uint64_t>(n) >> shift) & 0xFF);
		}

		co_await write_all(header.data(), header.size());

		if (n > 0)
			co_await write_all(data, n);
	}

	async::task<std::optional<message>> ws_connection::receive() {
		std::string accumulated;
		std::optional<std::uint8_t> message_opcode;

		for (;;) {
			if (closed_)
				co_return std::nullopt;

			std::optional<raw_frame> f = co_await read_frame();

			if (!f) {
				closed_ = true;
				co_return std::nullopt;
			}

			if (f->opcode == opcode_close) {
				co_await write_frame(true, opcode_close, f->payload.data(), f->payload.size());
				closed_ = true;
				co_return std::nullopt;
			}

			if (f->opcode == opcode_ping) {
				co_await write_frame(true, opcode_pong, f->payload.data(), f->payload.size());
				continue;
			}

			if (f->opcode == opcode_pong)
				continue;

			if (f->opcode == opcode_continuation) {
				if (!message_opcode) {
					co_await close(1002, "unexpected continuation frame");
					co_return std::nullopt;
				}

				accumulated += f->payload;
			}
			else if (f->opcode == opcode_text || f->opcode == opcode_binary) {
				message_opcode = f->opcode;
				accumulated = std::move(f->payload);
			}
			else {
				co_await close(1002, "unsupported opcode");
				co_return std::nullopt;
			}

			if (f->fin) {
				message msg;
				msg.type = (*message_opcode == opcode_text) ? message_type::text : message_type::binary;
				msg.data = std::move(accumulated);
				co_return msg;
			}
		}
	}

	async::task<void> ws_connection::send_text(std::string_view text) {
		co_await write_frame(true, opcode_text, text.data(), text.size());
	}

	async::task<void> ws_connection::send_binary(const void* data, std::size_t n) {
		co_await write_frame(true, opcode_binary, data, n);
	}

	async::task<void> ws_connection::close(std::uint16_t code, std::string_view reason) {
		if (closed_)
			co_return;

		std::string payload;
		payload += static_cast<char>((code >> 8) & 0xFF);
		payload += static_cast<char>(code & 0xFF);
		payload += reason;

		co_await write_frame(true, opcode_close, payload.data(), payload.size());
		closed_ = true;
	}

}
