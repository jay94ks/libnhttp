#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_query_string.hpp"

using namespace nhttp::protocol;

TEST_CASE("http_query_string parses key=value pairs and decodes them", "[protocol][http_query_string]") {
	auto qs = http_query_string::parse("a=1&b=hello%20world&c=");

	REQUIRE(qs.size() == 3);
	REQUIRE(*qs.get("a") == "1");
	REQUIRE(*qs.get("b") == "hello world");
	REQUIRE(*qs.get("c") == "");
	REQUIRE_FALSE(qs.isset("missing"));
}

TEST_CASE("http_query_string treats a key with no '=' as an empty value", "[protocol][http_query_string]") {
	auto qs = http_query_string::parse("flag&x=1");

	REQUIRE(qs.isset("flag"));
	REQUIRE(*qs.get("flag") == "");
}

TEST_CASE("http_query_string handles an empty query string", "[protocol][http_query_string]") {
	auto qs = http_query_string::parse("");
	REQUIRE(qs.size() == 0);
}
