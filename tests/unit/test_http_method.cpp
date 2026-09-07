#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_method.hpp"

using namespace nhttp::protocol;

TEST_CASE("well-known methods carry the expected semantic flags", "[protocol][http_method]") {
	REQUIRE(http_method::GET().is(http_method_flags::idempotent));
	REQUIRE(http_method::GET().is(http_method_flags::cacheable));
	REQUIRE_FALSE(http_method::GET().is(http_method_flags::request_content));

	REQUIRE(http_method::POST().is(http_method_flags::request_content));
	REQUIRE(http_method::POST().is(http_method_flags::alter_state));
	REQUIRE_FALSE(http_method::POST().is(http_method_flags::idempotent));

	REQUIRE(http_method::DELETE().is(http_method_flags::idempotent));
	REQUIRE_FALSE(http_method::DELETE().is(http_method_flags::request_content));

	REQUIRE(http_method::PUT().is(http_method_flags::idempotent));
	REQUIRE(http_method::PUT().is(http_method_flags::request_content));
}

TEST_CASE("a custom/unrecognized method name carries no flags but round-trips its name", "[protocol][http_method]") {
	http_method m(std::string("PROPFIND"));

	REQUIRE(m.name() == "PROPFIND");
	REQUIRE(m.flags() == http_method_flags::none);
}

TEST_CASE("http_method equality and ordering are name-based", "[protocol][http_method]") {
	REQUIRE(http_method(std::string("GET")) == http_method::GET());
	REQUIRE(http_method(std::string("get")) != http_method::GET()); // method names are case-sensitive per RFC 7230
	REQUIRE(http_method(std::string("A")) < http_method(std::string("B")));
}
