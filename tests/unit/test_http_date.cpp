#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_date.hpp"

using namespace nhttp::protocol;

TEST_CASE("format_http_date produces RFC 1123 form", "[protocol][http_date]") {
	// 2021-04-13 13:38:53 UTC, a Tuesday.
	const std::time_t t = 1618321133;
	REQUIRE(format_http_date(t) == "Tue, 13 Apr 2021 13:38:53 GMT");
}

TEST_CASE("parse_http_date is the inverse of format_http_date", "[protocol][http_date]") {
	const std::time_t original = 1618321133;
	const std::string formatted = format_http_date(original);

	REQUIRE(parse_http_date(formatted) == original);
}

TEST_CASE("parse_http_date rejects garbage input", "[protocol][http_date]") {
	REQUIRE(parse_http_date("not a date") == static_cast<std::time_t>(-1));
	REQUIRE(parse_http_date("") == static_cast<std::time_t>(-1));
}
