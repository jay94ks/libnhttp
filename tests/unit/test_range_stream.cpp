#include <catch2/catch_test_macros.hpp>

#include "nhttp/io/memory_stream.hpp"
#include "nhttp/io/range_stream.hpp"
#include "nhttp/async/sync_wait.hpp"

using namespace nhttp::io;
using nhttp::async::sync_wait;

namespace {

	std::shared_ptr<memory_stream> backing(const std::string& content) {
		return std::make_shared<memory_stream>(std::vector<std::uint8_t>(content.begin(), content.end()));
	}

}

TEST_CASE("range_stream exposes only the requested begin-to-end window of the inner stream", "[io][range_stream]") {
	range_stream rs(backing("0123456789"), 2, 5);

	REQUIRE(rs.get_length() == 3);

	char buf[16] = { 0 };
	const std::size_t n = sync_wait(rs.read(buf, sizeof(buf)));

	REQUIRE(n == 3);
	REQUIRE(std::string(buf, n) == "234");

	// end of the range reached: further reads return 0, even though the inner
	// stream has more bytes past the window.
	REQUIRE(sync_wait(rs.read(buf, sizeof(buf))) == 0);
}

TEST_CASE("range_stream clamps a requested end past the inner stream's length", "[io][range_stream]") {
	range_stream rs(backing("abc"), 1, 1000);

	REQUIRE(rs.get_length() == 2);

	std::string out;
	sync_wait(rs.read_all(out));
	REQUIRE(out == "bc");
}

TEST_CASE("range_stream falls back to an unbounded pass-through when begin/end are negative", "[io][range_stream]") {
	range_stream rs(backing("full-content"), -1, -1);

	REQUIRE(rs.get_length() == static_cast<std::int64_t>(std::string("full-content").size()));

	std::string out;
	sync_wait(rs.read_all(out));
	REQUIRE(out == "full-content");
}

TEST_CASE("range_stream::seek repositions within the window's own coordinate space", "[io][range_stream]") {
	range_stream rs(backing("0123456789"), 3, 8); // window: "34567"

	REQUIRE(sync_wait(rs.seek(2, seek_origin::begin)) == 2);

	char buf[16] = { 0 };
	const std::size_t n = sync_wait(rs.read(buf, sizeof(buf)));

	REQUIRE(std::string(buf, n) == "567");
}
