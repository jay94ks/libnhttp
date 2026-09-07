#include <catch2/catch_test_macros.hpp>

#include "nhttp/protocol/http_multipart.hpp"
#include "nhttp/io/memory_stream.hpp"
#include "nhttp/async/sync_wait.hpp"

using namespace nhttp::protocol;
using namespace nhttp::io;
using namespace nhttp::async;

namespace {

	std::shared_ptr<memory_stream> body_bytes(const std::string& raw) {
		return std::make_shared<memory_stream>(std::vector<std::uint8_t>(raw.begin(), raw.end()));
	}

	task<std::string> drain(std::shared_ptr<stream> s) {
		std::string out;
		co_await s->read_all(out);
		co_return out;
	}

}

TEST_CASE("multipart_reader parses a text field and a file part", "[protocol][http_multipart]") {
	const std::string boundary = "----WebKitFormBoundaryABC123";
	const std::string body =
		"--" + boundary + "\r\n"
		"Content-Disposition: form-data; name=\"field1\"\r\n"
		"\r\n"
		"value1\r\n"
		"--" + boundary + "\r\n"
		"Content-Disposition: form-data; name=\"file1\"; filename=\"a.txt\"\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"file content here\r\n"
		"--" + boundary + "--\r\n";

	multipart_reader reader(body_bytes(body), boundary);

	auto part1 = sync_wait(reader.next_part());
	REQUIRE(part1.has_value());
	REQUIRE(part1->name() == "field1");
	REQUIRE_FALSE(part1->filename().has_value());
	REQUIRE(sync_wait(drain(part1->body)) == "value1");

	auto part2 = sync_wait(reader.next_part());
	REQUIRE(part2.has_value());
	REQUIRE(part2->name() == "file1");
	REQUIRE(part2->filename() == "a.txt");
	REQUIRE(*part2->headers.get("Content-Type") == "text/plain");
	REQUIRE(sync_wait(drain(part2->body)) == "file content here");

	auto part3 = sync_wait(reader.next_part());
	REQUIRE_FALSE(part3.has_value());
}

TEST_CASE("multipart_reader::next_part discards an unread part's remaining body automatically", "[protocol][http_multipart]") {
	const std::string boundary = "BND";
	const std::string body =
		"--" + boundary + "\r\n"
		"Content-Disposition: form-data; name=\"a\"\r\n"
		"\r\n"
		"first-part-body-not-fully-read\r\n"
		"--" + boundary + "\r\n"
		"Content-Disposition: form-data; name=\"b\"\r\n"
		"\r\n"
		"second\r\n"
		"--" + boundary + "--\r\n";

	multipart_reader reader(body_bytes(body), boundary);

	auto part1 = sync_wait(reader.next_part());
	REQUIRE(part1.has_value());
	// deliberately do NOT read part1's body before moving on.

	auto part2 = sync_wait(reader.next_part());
	REQUIRE(part2.has_value());
	REQUIRE(part2->name() == "b");
	REQUIRE(sync_wait(drain(part2->body)) == "second");
}

TEST_CASE("multipart_reader handles a body with no preamble and a single part", "[protocol][http_multipart]") {
	const std::string boundary = "X";
	const std::string body =
		"--" + boundary + "\r\n"
		"Content-Disposition: form-data; name=\"only\"\r\n"
		"\r\n"
		"just one part\r\n"
		"--" + boundary + "--\r\n";

	multipart_reader reader(body_bytes(body), boundary);

	auto part = sync_wait(reader.next_part());
	REQUIRE(part.has_value());
	REQUIRE(sync_wait(drain(part->body)) == "just one part");

	REQUIRE_FALSE(sync_wait(reader.next_part()).has_value());
}

TEST_CASE("multipart_reader streams a body larger than its internal read chunk size", "[protocol][http_multipart]") {
	const std::string boundary = "BIG";
	const std::string large_content(10000, 'z');

	const std::string body =
		"--" + boundary + "\r\n"
		"Content-Disposition: form-data; name=\"big\"\r\n"
		"\r\n" +
		large_content + "\r\n"
		"--" + boundary + "--\r\n";

	multipart_reader reader(body_bytes(body), boundary);

	auto part = sync_wait(reader.next_part());
	REQUIRE(part.has_value());
	REQUIRE(sync_wait(drain(part->body)) == large_content);
}
