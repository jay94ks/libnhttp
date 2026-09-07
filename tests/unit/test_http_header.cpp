#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_header.hpp"

using namespace nhttp::protocol;

TEST_CASE("http_header::try_parse extracts name and trims the value", "[protocol][http_header]") {
	const std::string line = "Content-Type:   text/html  \r\n";
	http_header h;

	const std::ptrdiff_t consumed = http_header::try_parse(line.data(), line.size(), h);

	REQUIRE(consumed == static_cast<std::ptrdiff_t>(line.size()));
	REQUIRE(h.name == "Content-Type");
	REQUIRE(h.value == "text/html");
}

TEST_CASE("http_header::try_parse reports 0 (need more data) with no newline yet", "[protocol][http_header]") {
	const std::string partial = "Content-Length: 42";
	http_header h;

	REQUIRE(http_header::try_parse(partial.data(), partial.size(), h) == 0);
}

TEST_CASE("http_header::try_parse reports malformed lines", "[protocol][http_header]") {
	const std::string no_colon = "not-a-header-line\r\n";
	http_header h;

	REQUIRE(http_header::try_parse(no_colon.data(), no_colon.size(), h) < 0);
}

TEST_CASE("header_name_equals is case-insensitive", "[protocol][http_header]") {
	REQUIRE(header_name_equals("Content-Length", "content-length"));
	REQUIRE_FALSE(header_name_equals("Content-Length", "Content-Type"));
}

TEST_CASE("http_headers set/get/unset behave case-insensitively and set replaces", "[protocol][http_header]") {
	http_headers headers;
	headers.set("Content-Type", "text/plain");
	headers.set("content-type", "application/json"); // should replace, not duplicate

	REQUIRE(headers.size() == 1);
	REQUIRE(*headers.get("CONTENT-TYPE") == "application/json");

	headers.add("Set-Cookie", "a=1");
	headers.add("Set-Cookie", "b=2");
	REQUIRE(headers.get_all("set-cookie").size() == 2);

	headers.unset("content-type");
	REQUIRE_FALSE(headers.isset("Content-Type"));
}

TEST_CASE("http_headers::write_to serializes in insertion order", "[protocol][http_header]") {
	http_headers headers;
	headers.set("Host", "example.com");
	headers.set("Accept", "*/*");

	std::string out;
	headers.write_to(out);

	REQUIRE(out == "Host: example.com\r\nAccept: */*\r\n");
}
