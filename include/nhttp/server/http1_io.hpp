#pragma once

#include "../protocol/http_header.hpp"
#include "../io/stream.hpp"
#include "../async/task.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace nhttp::server::http1_io {

	/**
	 * class buffered_wire_stream.
	 * presents "whatever's left in a read-ahead buffer, then straight from the
	 * wire" as one stream — the shape every HTTP/1.1 reader in this codebase
	 * needs (a connection's already-buffered bytes past the header block, or
	 * the reverse proxy's own upstream-response leftover bytes). references
	 * into the caller's own buffer/wire, so it must not outlive them.
	 */
	class buffered_wire_stream final : public io::stream {
	public:
		buffered_wire_stream(std::string& leftover, io::stream& wire) noexcept
			: leftover_(leftover), wire_(wire)
		{
		}

		std::int64_t get_length() const override { return -1; }
		bool can_seek() const noexcept override { return false; }

		async::task<std::int64_t> seek(std::int64_t, io::seek_origin) override { co_return -1; }

		async::task<std::size_t> read(void* buf, std::size_t n) override {
			if (!leftover_.empty()) {
				const std::size_t to_copy = std::min(n, leftover_.size());
				std::memcpy(buf, leftover_.data(), to_copy);
				leftover_.erase(0, to_copy);
				co_return to_copy;
			}

			co_return co_await wire_.read(buf, n);
		}

		async::task<std::size_t> write(const void*, std::size_t) override { co_return 0; }
		async::task<void> flush() override { co_return; }
		async::task<void> close() override { co_return; }

	private:
		std::string& leftover_;
		io::stream& wire_;
	};

	/**
	 * class owned_buffered_wire_stream.
	 * the same "leftover buffer, then straight from the wire" shape as
	 * buffered_wire_stream, but owns a moved-in copy of the leftover bytes
	 * and a shared_ptr to the wire instead of referencing a caller's own
	 * fields — for callers with no long-lived object to reference from (see
	 * make_owned_body_stream).
	 */
	class owned_buffered_wire_stream final : public io::stream {
	public:
		owned_buffered_wire_stream(std::string leftover, std::shared_ptr<io::stream> wire) noexcept
			: leftover_(std::move(leftover)), wire_(std::move(wire))
		{
		}

		std::int64_t get_length() const override { return -1; }
		bool can_seek() const noexcept override { return false; }

		async::task<std::int64_t> seek(std::int64_t, io::seek_origin) override { co_return -1; }

		async::task<std::size_t> read(void* buf, std::size_t n) override {
			if (!leftover_.empty()) {
				const std::size_t to_copy = std::min(n, leftover_.size());
				std::memcpy(buf, leftover_.data(), to_copy);
				leftover_.erase(0, to_copy);
				co_return to_copy;
			}

			co_return co_await wire_->read(buf, n);
		}

		async::task<std::size_t> write(const void*, std::size_t) override { co_return 0; }
		async::task<void> flush() override { co_return; }
		async::task<void> close() override { co_return; }

	private:
		std::string leftover_;
		std::shared_ptr<io::stream> wire_;
	};

	/* reads bytes from `read_buffer`+`wire` (the buffered_wire_stream pattern,
	 * inlined here since it also needs to consume straight from `read_buffer`
	 * before any of it becomes a stream) until a full header block (a blank
	 * line) has been consumed, filling `out`. shared by connection.cpp (the
	 * server role) and reverse_proxy.cpp (the client role reading an
	 * upstream's response headers), so header-parsing rules can't silently
	 * diverge between them. */
	async::task<bool> read_headers(std::string& read_buffer, io::stream& wire, protocol::http_headers& out, std::size_t max_header_size);

	/* builds a body stream from `headers` (a chunked-decoder, or an
	 * exact-length window over whatever's left of `read_buffer`+`wire`) — the
	 * same framing decision needed whether reading a request body (server
	 * role) or an upstream's response body (reverse-proxy client role).
	 *
	 * WARNING: the result (a buffered_wire_stream, possibly wrapped in a
	 * decoder) holds a *reference* to `read_buffer` and `wire`'s pointee —
	 * safe only when the caller has a long-lived object those actually live
	 * in (e.g. connection's own read_buffer_/wire_ members, alive for the
	 * whole connection) that will outlive the returned stream. A caller whose
	 * own locals would go out of scope before the returned stream is fully
	 * read (e.g. reverse_proxy's on_handle, whose coroutine frame ends before
	 * the proxied response body is written back to the client) must use
	 * make_owned_body_stream() instead. */
	std::shared_ptr<io::stream> make_body_stream(std::string& read_buffer, std::shared_ptr<io::stream> wire, const protocol::http_headers& headers);

	/* same framing decision as make_body_stream(), but the result *owns* a
	 * moved-in copy of the leftover bytes and a shared_ptr to `wire`, so its
	 * lifetime never depends on the caller's own stack frame — use this
	 * whenever there's no long-lived object like connection's read_buffer_/
	 * wire_ to safely reference instead (see make_body_stream's warning). */
	std::shared_ptr<io::stream> make_owned_body_stream(std::string leftover, std::shared_ptr<io::stream> wire, const protocol::http_headers& headers);

	/* writes every byte of `buf` to `wire`, retrying partial writes. */
	async::task<void> write_all(io::stream& wire, const void* buf, std::size_t n);

	/* writes `body` to `wire` framed the way `content_length` says to: chunked
	 * transfer-coding if negative (unknown length), otherwise a plain copy of
	 * exactly `content_length` bytes. does not write any headers — the caller
	 * decides Transfer-Encoding/Content-Length and writes the header block
	 * itself, since a server response and a proxied request/response header
	 * set differ in nearly everything except this body-framing rule. */
	async::task<void> write_message_body(io::stream& wire, io::stream& body, std::int64_t content_length);

}
