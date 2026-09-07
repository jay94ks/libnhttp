#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_chunked.hpp"
#include "nhttp/io/memory_stream.hpp"
#include "nhttp/async/sync_wait.hpp"

using namespace nhttp::protocol;
using namespace nhttp::io;
using nhttp::async::sync_wait;

namespace {

	std::shared_ptr<memory_stream> wire_bytes(const std::string& raw) {
		return std::make_shared<memory_stream>(std::vector<std::uint8_t>(raw.begin(), raw.end()));
	}

}

TEST_CASE("format_chunk_header formats sizes as lowercase hex", "[protocol][http_chunked]") {
	REQUIRE(format_chunk_header(0) == "0\r\n");
	REQUIRE(format_chunk_header(26) == "1a\r\n");
	REQUIRE(format_chunk_header(255) == "ff\r\n");
}

TEST_CASE("chunked_decoder_stream decodes multiple chunks into a contiguous body", "[protocol][http_chunked]") {
	const std::string wire = "4\r\nWiki\r\n5\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n";
	chunked_decoder_stream decoder(wire_bytes(wire));

	std::string out;
	sync_wait(decoder.read_all(out));

	REQUIRE(out == "Wikipedia in\r\n\r\nchunks.");
}

TEST_CASE("chunked_decoder_stream stops after the final 0-length chunk and consumes trailers", "[protocol][http_chunked]") {
	const std::string wire = "3\r\nabc\r\n0\r\nX-Trailer: value\r\n\r\n";
	chunked_decoder_stream decoder(wire_bytes(wire));

	std::string out;
	sync_wait(decoder.read_all(out));

	REQUIRE(out == "abc");
}

TEST_CASE("chunked_decoder_stream ignores chunk extensions after ';'", "[protocol][http_chunked]") {
	const std::string wire = "3;ext=1\r\nabc\r\n0\r\n\r\n";
	chunked_decoder_stream decoder(wire_bytes(wire));

	std::string out;
	sync_wait(decoder.read_all(out));

	REQUIRE(out == "abc");
}

TEST_CASE("chunked round-trip: encode with format_chunk_header, then decode back", "[protocol][http_chunked]") {
	std::string wire;
	for (const std::string& part : { std::string("hello "), std::string("chunked "), std::string("world") }) {
		wire += format_chunk_header(part.size());
		wire += part;
		wire += chunk_data_terminator;
	}
	wire += chunked_body_terminator;

	chunked_decoder_stream decoder(wire_bytes(wire));
	std::string out;
	sync_wait(decoder.read_all(out));

	REQUIRE(out == "hello chunked world");
}
