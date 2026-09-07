#include "nhttp/protocol/urlencode.hpp"

namespace nhttp::protocol {

	namespace {

		int hex_value(char c) noexcept {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		bool is_unreserved(unsigned char c) noexcept {
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
				return true;

			return c == '-' || c == '_' || c == '.' || c == '~';
		}

		char to_hex_digit(int v) noexcept {
			return static_cast<char>(v < 10 ? ('0' + v) : ('A' + (v - 10)));
		}

	}

	std::string url_decode(std::string_view input) {
		std::string out;
		out.reserve(input.size());

		for (std::size_t i = 0; i < input.size(); ++i) {
			const char c = input[i];

			if (c == '+') {
				out += ' ';
				continue;
			}

			if (c == '%' && i + 2 < input.size()) {
				const int hi = hex_value(input[i + 1]);
				const int lo = hex_value(input[i + 2]);

				if (hi >= 0 && lo >= 0) {
					out += static_cast<char>((hi << 4) | lo);
					i += 2;
					continue;
				}
			}

			out += c;
		}

		return out;
	}

	std::string url_encode(std::string_view input) {
		std::string out;
		out.reserve(input.size());

		for (const char raw : input) {
			const unsigned char c = static_cast<unsigned char>(raw);

			if (is_unreserved(c)) {
				out += static_cast<char>(c);
			}
			else if (c == ' ') {
				out += '+';
			}
			else {
				out += '%';
				out += to_hex_digit((c >> 4) & 0x0F);
				out += to_hex_digit(c & 0x0F);
			}
		}

		return out;
	}

}
