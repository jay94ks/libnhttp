#include "nhttp/ws/sha1.hpp"

#include <vector>

namespace nhttp::ws {

	namespace {

		std::uint32_t rotl(std::uint32_t v, int bits) noexcept {
			return (v << bits) | (v >> (32 - bits));
		}

	}

	std::array<std::uint8_t, 20> sha1(std::string_view data) {
		std::uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

		std::vector<std::uint8_t> msg(data.begin(), data.end());
		const std::uint64_t bit_length = static_cast<std::uint64_t>(msg.size()) * 8;

		msg.push_back(0x80);

		while (msg.size() % 64 != 56)
			msg.push_back(0);

		for (int i = 7; i >= 0; --i)
			msg.push_back(static_cast<std::uint8_t>((bit_length >> (i * 8)) & 0xFF));

		for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
			std::uint32_t w[80];

			for (int i = 0; i < 16; ++i) {
				const std::size_t o = chunk + static_cast<std::size_t>(i) * 4;
				w[i] = (static_cast<std::uint32_t>(msg[o]) << 24) | (static_cast<std::uint32_t>(msg[o + 1]) << 16) |
					(static_cast<std::uint32_t>(msg[o + 2]) << 8) | static_cast<std::uint32_t>(msg[o + 3]);
			}

			for (int i = 16; i < 80; ++i)
				w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

			std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

			for (int i = 0; i < 80; ++i) {
				std::uint32_t f, k;

				if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
				else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
				else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
				else { f = b ^ c ^ d; k = 0xCA62C1D6u; }

				const std::uint32_t temp = rotl(a, 5) + f + e + k + w[i];
				e = d; d = c; c = rotl(b, 30); b = a; a = temp;
			}

			h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
		}

		std::array<std::uint8_t, 20> digest{};

		auto put = [&digest](std::size_t idx, std::uint32_t h) {
			digest[idx] = static_cast<std::uint8_t>((h >> 24) & 0xFF);
			digest[idx + 1] = static_cast<std::uint8_t>((h >> 16) & 0xFF);
			digest[idx + 2] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
			digest[idx + 3] = static_cast<std::uint8_t>(h & 0xFF);
		};

		put(0, h0); put(4, h1); put(8, h2); put(12, h3); put(16, h4);

		return digest;
	}

}
