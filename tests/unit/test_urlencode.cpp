#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/urlencode.hpp"

using namespace nhttp::protocol;

TEST_CASE("url_encode/url_decode round-trip reserved and unreserved characters", "[protocol][urlencode]") {
	const std::string original = "hello world/&?=+ 한글";
	const std::string encoded = url_encode(original);
	const std::string decoded = url_decode(encoded);

	REQUIRE(decoded == original);
	REQUIRE(encoded.find(' ') == std::string::npos);
}

TEST_CASE("url_decode handles '+' as space and %XX escapes", "[protocol][urlencode]") {
	REQUIRE(url_decode("a+b") == "a b");
	REQUIRE(url_decode("a%20b") == "a b");
	REQUIRE(url_decode("100%25") == "100%");
}

TEST_CASE("url_decode passes through a malformed escape verbatim", "[protocol][urlencode]") {
	REQUIRE(url_decode("100%zz") == "100%zz");
}
