#include <catch2/catch_test_macros.hpp>

#include "nhttp/io/memory_stream.hpp"
#include "nhttp/async/sync_wait.hpp"

using namespace nhttp::io;
using nhttp::async::sync_wait;

TEST_CASE("memory_stream writes, reads back, and reports length", "[io][memory_stream]") {
	memory_stream ms;

	const char message[] = "hello, stream";
	const std::size_t written = sync_wait(ms.write(message, sizeof(message) - 1));
	REQUIRE(written == sizeof(message) - 1);
	REQUIRE(ms.get_length() == static_cast<std::int64_t>(sizeof(message) - 1));

	sync_wait(ms.seek(0, seek_origin::begin));

	char buf[32] = { 0 };
	const std::size_t n = sync_wait(ms.read(buf, sizeof(buf)));
	REQUIRE(n == sizeof(message) - 1);
	REQUIRE(std::string(buf, n) == "hello, stream");

	// reading again from the same (now end-of-stream) position yields 0.
	REQUIRE(sync_wait(ms.read(buf, sizeof(buf))) == 0);
}

TEST_CASE("memory_stream::seek supports begin/current/end and rejects out-of-range targets", "[io][memory_stream]") {
	memory_stream ms(std::vector<std::uint8_t>{ 'a', 'b', 'c', 'd', 'e' });

	REQUIRE(sync_wait(ms.seek(2, seek_origin::begin)) == 2);
	REQUIRE(sync_wait(ms.seek(1, seek_origin::current)) == 3);
	REQUIRE(sync_wait(ms.seek(-1, seek_origin::end)) == 4);
	REQUIRE(sync_wait(ms.seek(-100, seek_origin::begin)) == -1);
	REQUIRE(sync_wait(ms.seek(100, seek_origin::begin)) == -1);
}

TEST_CASE("memory_stream::read_all collects the whole buffer", "[io][memory_stream]") {
	memory_stream ms(std::vector<std::uint8_t>{ 'x', 'y', 'z' });

	std::string out;
	sync_wait(ms.read_all(out));

	REQUIRE(out == "xyz");
}
