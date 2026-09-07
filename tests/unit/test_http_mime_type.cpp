#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_mime_type.hpp"

using namespace nhttp::protocol;

TEST_CASE("mime_type::parse splits essence and parameters", "[protocol][http_mime_type]") {
	auto m = mime_type::parse("multipart/form-data; boundary=----WebKitFormBoundaryABC123");

	REQUIRE(m.type == "multipart");
	REQUIRE(m.subtype == "form-data");
	REQUIRE(m.essence() == "multipart/form-data");
	REQUIRE(*m.parameter("boundary") == "----WebKitFormBoundaryABC123");
}

TEST_CASE("mime_type::parse handles a quoted parameter value", "[protocol][http_mime_type]") {
	auto m = mime_type::parse("text/plain; charset=\"utf-8\"");

	REQUIRE(m.essence() == "text/plain");
	REQUIRE(*m.parameter("charset") == "utf-8");
}

TEST_CASE("mime_type::parse handles no parameters at all", "[protocol][http_mime_type]") {
	auto m = mime_type::parse("application/json");

	REQUIRE(m.essence() == "application/json");
	REQUIRE(m.parameter("charset") == nullptr);
}

TEST_CASE("mime_type_from_extension recognizes common extensions case-insensitively", "[protocol][http_mime_type]") {
	REQUIRE(mime_type_from_extension("/index.html") == mime_types::TEXT_HTML);
	REQUIRE(mime_type_from_extension("/style.CSS") == mime_types::TEXT_CSS);
	REQUIRE(mime_type_from_extension("/data.json") == mime_types::APPLICATION_JSON);
	REQUIRE(mime_type_from_extension("/photo.JPG") == mime_types::IMAGE_JPEG);
}

TEST_CASE("mime_type_from_extension falls back to octet-stream for unknown or missing extensions", "[protocol][http_mime_type]") {
	REQUIRE(mime_type_from_extension("/no_extension") == mime_types::APPLICATION_OCTET_STREAM);
	REQUIRE(mime_type_from_extension("/file.unknownext") == mime_types::APPLICATION_OCTET_STREAM);
}
