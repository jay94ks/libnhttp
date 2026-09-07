#include <catch2/catch_test_macros.hpp>

#include "nhttp/ws/sha1.hpp"
#include "nhttp/ws/handshake.hpp"

#include <sstream>
#include <iomanip>

using namespace nhttp::ws;

namespace {

	std::string to_hex(const std::array<std::uint8_t, 20>& digest) {
		std::ostringstream out;

		for (const std::uint8_t b : digest)
			out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);

		return out.str();
	}

}

TEST_CASE("sha1 matches known test vectors", "[ws][sha1]") {
	REQUIRE(to_hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	REQUIRE(to_hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
	REQUIRE(to_hex(sha1("The quick brown fox jumps over the lazy dog")) == "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
}

TEST_CASE("compute_accept_key matches the RFC 6455 worked example", "[ws][handshake]") {
	// RFC 6455 section 1.3's canonical example.
	REQUIRE(compute_accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}
