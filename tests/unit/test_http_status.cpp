#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_status.hpp"

using namespace nhttp::protocol;

TEST_CASE("http_status reports the standard reason phrase", "[protocol][http_status]") {
	REQUIRE(http_status(200).reason_phrase() == "OK");
	REQUIRE(http_status(404).reason_phrase() == "Not Found");
	REQUIRE(http_status(206).reason_phrase() == "Partial Content");
	REQUIRE(http_status(405).reason_phrase() == "Method Not Allowed");
}

TEST_CASE("http_status falls back to Unknown for unrecognized codes", "[protocol][http_status]") {
	REQUIRE(http_status(799).reason_phrase() == "Unknown");
}

TEST_CASE("http_status::write_status_line formats the full status line", "[protocol][http_status]") {
	std::string out;
	http_status(404).write_status_line(out, 1);

	REQUIRE(out == "HTTP/1.1 404 Not Found\r\n");
}
