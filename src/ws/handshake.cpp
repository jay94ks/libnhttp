#include "nhttp/ws/handshake.hpp"
#include "nhttp/ws/sha1.hpp"

namespace nhttp::ws {

	namespace {

		constexpr std::string_view magic_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

		std::string base64_encode(const std::uint8_t* data, std::size_t len) {
			static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

			std::string out;
			out.reserve(((len + 2) / 3) * 4);

			std::size_t i = 0;

			while (i + 3 <= len) {
				const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
					(static_cast<std::uint32_t>(data[i + 1]) << 8) | static_cast<std::uint32_t>(data[i + 2]);

				out += table[(n >> 18) & 0x3F];
				out += table[(n >> 12) & 0x3F];
				out += table[(n >> 6) & 0x3F];
				out += table[n & 0x3F];
				i += 3;
			}

			const std::size_t remaining = len - i;

			if (remaining == 1) {
				const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
				out += table[(n >> 18) & 0x3F];
				out += table[(n >> 12) & 0x3F];
				out += "==";
			}
			else if (remaining == 2) {
				const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8);
				out += table[(n >> 18) & 0x3F];
				out += table[(n >> 12) & 0x3F];
				out += table[(n >> 6) & 0x3F];
				out += '=';
			}

			return out;
		}

	}

	std::string compute_accept_key(std::string_view client_key) {
		std::string combined(client_key);
		combined += magic_guid;

		const std::array<std::uint8_t, 20> digest = sha1(combined);
		return base64_encode(digest.data(), digest.size());
	}

}
