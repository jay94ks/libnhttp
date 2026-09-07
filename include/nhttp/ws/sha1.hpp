#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nhttp::ws {

	/* a minimal, self-contained SHA-1 (FIPS 180-1) implementation — the WebSocket
	 * handshake (RFC 6455) is its only consumer here; not for anything security-
	 * sensitive (SHA-1 is broken for that). returns the 20-byte digest. */
	std::array<std::uint8_t, 20> sha1(std::string_view data);

}
