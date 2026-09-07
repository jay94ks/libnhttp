#include "nhttp/server/http1_io.hpp"
#include "nhttp/protocol/http_chunked.hpp"
#include "nhttp/io/file_stream.hpp"
#include "nhttp/io/memory_stream.hpp"
#include "nhttp/io/range_stream.hpp"
#include "nhttp/io/socket_stream.hpp"
#include "nhttp/platform/socket.hpp"

namespace nhttp::server::http1_io {

	namespace {

		/* the sendfile(2) fast path from PLAN.md's P1: a zero-userspace-copy,
		 * no-thread-hop send straight from the file's page cache to the
		 * socket, instead of write_message_body's generic loop (thread-pool
		 * read into a buffer, then write that buffer out). Only reachable when
		 * write_message_body's caller already established every precondition
		 * (plain file body, plain TCP wire, non-negative content_length,
		 * platform support) — see its call site below. */
		async::task<void> write_message_body_sendfile(io::socket_stream& wire, io::file_stream& body, std::int64_t content_length) {
			std::int64_t offset = 0; // file_stream::open() never seeks, so a fresh body starts at 0.
			std::int64_t remaining = content_length;

			while (remaining > 0) {
				const std::size_t want = static_cast<std::size_t>(remaining);
				const std::size_t sent = co_await wire.socket().send_file(body.native_fd(), offset, want);

				if (sent == 0)
					break; // short file vs. a caller-claimed content_length — same "stop early" behavior the generic loop has on body.read() == 0.

				remaining -= static_cast<std::int64_t>(sent);
			}
		}

	}

	async::task<bool> read_headers(std::string& read_buffer, io::stream& wire, protocol::http_headers& out, std::size_t max_header_size) {
		for (;;) {
			std::size_t nl = read_buffer.find('\n');

			while (nl == std::string::npos) {
				if (read_buffer.size() > max_header_size)
					co_return false;

				char chunk[4096];
				const std::size_t n = co_await wire.read(chunk, sizeof(chunk));

				if (n == 0)
					co_return false;

				read_buffer.append(chunk, n);
				nl = read_buffer.find('\n');
			}

			std::size_t line_len = nl;

			if (line_len > 0 && read_buffer[line_len - 1] == '\r')
				--line_len;

			if (line_len == 0) {
				read_buffer.erase(0, nl + 1);
				co_return true;
			}

			protocol::http_header h;
			const std::ptrdiff_t consumed = protocol::http_header::try_parse(read_buffer.data(), read_buffer.size(), h);

			if (consumed <= 0)
				co_return false;

			out.add(std::move(h.name), std::move(h.value));
			read_buffer.erase(0, static_cast<std::size_t>(consumed));
		}
	}

	std::shared_ptr<io::stream> make_body_stream(std::string& read_buffer, std::shared_ptr<io::stream> wire, const protocol::http_headers& headers) {
		auto source = std::make_shared<buffered_wire_stream>(read_buffer, *wire);

		if (const std::string* te = headers.get(protocol::header_names::TRANSFER_ENCODING)) {
			if (protocol::header_value_contains_token(*te, "chunked"))
				return std::make_shared<protocol::chunked_decoder_stream>(std::move(source));
		}

		if (const std::string* cl = headers.get(protocol::header_names::CONTENT_LENGTH)) {
			std::int64_t length = 0;

			for (const char c : *cl) {
				if (c < '0' || c > '9')
					return nullptr; // malformed Content-Length

				length = length * 10 + (c - '0');
			}

			return std::make_shared<io::range_stream>(std::move(source), 0, length);
		}

		return std::make_shared<io::memory_stream>();
	}

	std::shared_ptr<io::stream> make_owned_body_stream(std::string leftover, std::shared_ptr<io::stream> wire, const protocol::http_headers& headers) {
		auto source = std::make_shared<owned_buffered_wire_stream>(std::move(leftover), std::move(wire));

		if (const std::string* te = headers.get(protocol::header_names::TRANSFER_ENCODING)) {
			if (protocol::header_value_contains_token(*te, "chunked"))
				return std::make_shared<protocol::chunked_decoder_stream>(std::move(source));
		}

		if (const std::string* cl = headers.get(protocol::header_names::CONTENT_LENGTH)) {
			std::int64_t length = 0;

			for (const char c : *cl) {
				if (c < '0' || c > '9')
					return nullptr; // malformed Content-Length

				length = length * 10 + (c - '0');
			}

			return std::make_shared<io::range_stream>(std::move(source), 0, length);
		}

		return std::make_shared<io::memory_stream>();
	}

	async::task<void> write_all(io::stream& wire, const void* buf, std::size_t n) {
		const char* p = static_cast<const char*>(buf);
		std::size_t written = 0;

		while (written < n)
			written += co_await wire.write(p + written, n - written);
	}

	async::task<void> write_message_body(io::stream& wire, io::stream& body, std::int64_t content_length) {
		char buf[4096];

		io::file_stream* const file_body = (content_length > 0) ? dynamic_cast<io::file_stream*>(&body) : nullptr;
		io::socket_stream* const socket_wire = file_body ? dynamic_cast<io::socket_stream*>(&wire) : nullptr;

		if (content_length < 0) {
			for (;;) {
				const std::size_t got = co_await body.read(buf, sizeof(buf));

				if (got == 0)
					break;

				const std::string chunk_head = protocol::format_chunk_header(got);
				co_await write_all(wire, chunk_head.data(), chunk_head.size());
				co_await write_all(wire, buf, got);
				co_await write_all(wire, protocol::chunk_data_terminator.data(), protocol::chunk_data_terminator.size());
			}

			co_await write_all(wire, protocol::chunked_body_terminator.data(), protocol::chunked_body_terminator.size());
		}
		else if (socket_wire && platform::socket_handle::supports_send_file()) {
			// plain file over plain TCP (never TLS — no kernel-level sendfile
			// equivalent there) with a known length and no transform: the
			// sendfile(2) fast path. A byte-Range response's file_stream is
			// wrapped in a range_stream, which fails the file_body cast above
			// and correctly falls through to the generic path below instead.
			co_await write_message_body_sendfile(*socket_wire, *file_body, content_length);
		}
		else {
			std::int64_t remaining = content_length;

			while (remaining > 0) {
				const std::size_t want = static_cast<std::size_t>(std::min<std::int64_t>(remaining, static_cast<std::int64_t>(sizeof(buf))));
				const std::size_t got = co_await body.read(buf, want);

				if (got == 0)
					break;

				co_await write_all(wire, buf, got);
				remaining -= static_cast<std::int64_t>(got);
			}
		}
	}

}
