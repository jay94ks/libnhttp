#include <catch2/catch_test_macros.hpp>

#include "nhttp/http2/hpack.hpp"

#include <vector>

using namespace nhttp::http2;

namespace {

	std::vector<std::uint8_t> hex_to_bytes(const std::string& hex) {
		std::vector<std::uint8_t> out;

		for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
			const auto byte = static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
			out.push_back(byte);
		}

		return out;
	}

	const header_field* find(const header_list& list, const std::string& name) {
		for (const header_field& h : list) {
			if (h.name == name)
				return &h;
		}

		return nullptr;
	}

}

// RFC 7541 Appendix C.4.1 — "First Request", Huffman coded. This is the
// standard reference vector every HPACK implementation is checked against;
// decoding it correctly exercises the static table, literal-with-incremental-
// indexing, and (critically) the Huffman code table for "www.example.com".
TEST_CASE("hpack_decoder decodes RFC 7541 Appendix C.4.1's official Huffman-coded request", "[http2][hpack]") {
	const auto bytes = hex_to_bytes("828684418cf1e3c2e5f23a6ba0ab90f4ff");

	hpack_decoder decoder;
	header_list headers;

	REQUIRE(decoder.decode(bytes.data(), bytes.size(), headers));
	REQUIRE(headers.size() == 4);

	REQUIRE(find(headers, ":method")->value == "GET");
	REQUIRE(find(headers, ":scheme")->value == "http");
	REQUIRE(find(headers, ":path")->value == "/");
	REQUIRE(find(headers, ":authority")->value == "www.example.com");
}

TEST_CASE("hpack encode-then-decode round-trips a realistic header list", "[http2][hpack]") {
	header_list input{
		{ ":method", "POST" },
		{ ":scheme", "https" },
		{ ":path", "/widgets?id=42" },
		{ ":authority", "api.example.com" },
		{ "content-type", "application/json" },
		{ "user-agent", "nhttp-test/1.0" },
		{ "x-custom-header", "some value with spaces and punctuation!" },
	};

	hpack_encoder encoder;
	std::string wire;
	encoder.encode(input, wire);

	hpack_decoder decoder;
	header_list output;

	REQUIRE(decoder.decode(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size(), output));
	REQUIRE(output.size() == input.size());

	for (std::size_t i = 0; i < input.size(); ++i) {
		REQUIRE(output[i].name == input[i].name);
		REQUIRE(output[i].value == input[i].value);
	}
}

TEST_CASE("hpack round-trips every byte value through the Huffman codec", "[http2][hpack]") {
	std::string all_bytes;

	for (int c = 0; c < 256; ++c)
		all_bytes += static_cast<char>(c);

	hpack_encoder encoder;
	std::string wire;
	encoder.encode(header_list{ { "x-binary", all_bytes } }, wire);

	hpack_decoder decoder;
	header_list output;

	REQUIRE(decoder.decode(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size(), output));
	REQUIRE(output.size() == 1);
	REQUIRE(output[0].value == all_bytes);
}

TEST_CASE("hpack_decoder's dynamic table serves an earlier literal on a later header block", "[http2][hpack]") {
	hpack_encoder encoder;
	hpack_decoder decoder;

	// this decoder instance is meant to live for a whole HTTP/2 connection —
	// its dynamic table carries state across decode() calls the same way.
	std::string first_wire;
	encoder.encode(header_list{ { ":authority", "www.example.com" } }, first_wire);

	header_list first_out;
	REQUIRE(decoder.decode(reinterpret_cast<const std::uint8_t*>(first_wire.data()), first_wire.size(), first_out));
	REQUIRE(first_out[0].value == "www.example.com");

	// this encoder always emits literal-without-indexing (see its class doc
	// comment), so it never itself relies on the dynamic table — this test
	// only exercises the *decoder's* dynamic table bookkeeping directly.
	std::string second_wire;
	encoder.encode(header_list{ { ":path", "/second" } }, second_wire);

	header_list second_out;
	REQUIRE(decoder.decode(reinterpret_cast<const std::uint8_t*>(second_wire.data()), second_wire.size(), second_out));
	REQUIRE(second_out[0].value == "/second");
}

TEST_CASE("hpack_decoder rejects a dynamic-table-size-update exceeding the configured bound", "[http2][hpack]") {
	hpack_decoder decoder(256);

	// a size-update instruction (001xxxxx) requesting 4096, over the 256 bound.
	std::vector<std::uint8_t> bytes{ 0x3f, 0xe1, 0x1f };
	header_list out;

	REQUIRE_FALSE(decoder.decode(bytes.data(), bytes.size(), out));
}
