#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_resource.hpp"

using namespace nhttp::protocol;

TEST_CASE("http_resource::try_parse parses method, decoded path, query, and version", "[protocol][http_resource]") {
	const std::string line = "GET /a%20b?x=1&y=2 HTTP/1.1\r\n";
	http_resource res;

	const std::ptrdiff_t consumed = http_resource::try_parse(line.data(), line.size(), res);

	REQUIRE(consumed == static_cast<std::ptrdiff_t>(line.size()));
	REQUIRE(res.method == http_method::GET());
	REQUIRE(res.path == "/a b");
	REQUIRE(res.raw_path == "/a%20b");
	REQUIRE(*res.query.get("x") == "1");
	REQUIRE(*res.query.get("y") == "2");
	REQUIRE(res.http_major == 1);
	REQUIRE(res.http_minor == 1);
}

TEST_CASE("http_resource::try_parse handles a path with no query string", "[protocol][http_resource]") {
	const std::string line = "POST /submit HTTP/1.1\r\n";
	http_resource res;

	REQUIRE(http_resource::try_parse(line.data(), line.size(), res) > 0);
	REQUIRE(res.method == http_method::POST());
	REQUIRE(res.path == "/submit");
	REQUIRE(res.query.size() == 0);
}

TEST_CASE("http_resource::try_parse reports 0 with no newline yet", "[protocol][http_resource]") {
	const std::string partial = "GET /still-arriving";
	http_resource res;

	REQUIRE(http_resource::try_parse(partial.data(), partial.size(), res) == 0);
}

TEST_CASE("http_resource::try_parse rejects a malformed request line", "[protocol][http_resource]") {
	const std::string bad = "GARBAGE\r\n";
	http_resource res;

	REQUIRE(http_resource::try_parse(bad.data(), bad.size(), res) < 0);
}
