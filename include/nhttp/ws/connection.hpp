#pragma once

#include "../io/stream.hpp"
#include "../async/task.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace nhttp::ws {

	enum class message_type { text, binary };

	struct message {
		message_type type;
		std::string data;
	};

	/**
	 * class ws_connection.
	 * a real RFC 6455 frame codec over an already-upgraded connection stream:
	 * fragmentation reassembly, masking/unmasking (server receives masked
	 * frames from the client and always sends unmasked frames back, per spec),
	 * and automatic ping->pong / close-handshake handling — the part the
	 * original implementation never actually delivered (see CONCEPTS.md §6).
	 */
	class ws_connection {
	public:
		/* `initial_buffer` is any bytes already read past the HTTP Upgrade
		 * request/response by the connection that's handing off to this one. */
		explicit ws_connection(std::shared_ptr<io::stream> wire, std::string initial_buffer = std::string());

	public:
		/* the next complete (reassembled) message, or nullopt once the
		 * connection is closed (either peer-initiated or after close()). ping
		 * frames are answered with pong automatically and never surfaced here. */
		async::task<std::optional<message>> receive();

		async::task<void> send_text(std::string_view text);
		async::task<void> send_binary(const void* data, std::size_t n);

		/* sends a close frame (unless already closed) and marks the connection closed. */
		async::task<void> close(std::uint16_t code = 1000, std::string_view reason = std::string_view());

		bool is_closed() const noexcept { return closed_; }

	private:
		struct raw_frame {
			bool fin;
			std::uint8_t opcode;
			std::string payload;
		};

		async::task<bool> fill_more();
		async::task<bool> read_exact(std::string& out, std::size_t n);
		async::task<std::optional<raw_frame>> read_frame();
		async::task<void> write_all(const void* data, std::size_t n);
		async::task<void> write_frame(bool fin, std::uint8_t opcode, const void* data, std::size_t n);

		std::shared_ptr<io::stream> wire_;
		std::string buffer_;
		bool eof_ = false;
		bool closed_ = false;
	};

}
