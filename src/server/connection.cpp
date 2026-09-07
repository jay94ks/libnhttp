#include "nhttp/server/connection.hpp"
#include "nhttp/protocol/http_chunked.hpp"
#include "nhttp/protocol/http_date.hpp"
#include "nhttp/protocol/urlencode.hpp"
#include "nhttp/io/memory_stream.hpp"
#include "nhttp/io/range_stream.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>

namespace nhttp::server {

	namespace {

		/* the remaining bytes of the connection's incoming byte stream after
		 * request-line/header parsing: whatever's left in the connection's own
		 * read-ahead buffer, then straight from the wire. references into the
		 * owning connection's state, so it must not outlive it. */
		class remaining_wire_stream final : public io::stream {
		public:
			remaining_wire_stream(std::string& leftover, io::stream& wire) noexcept
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

		bool header_value_contains_token(const std::string& value, std::string_view token) noexcept {
			std::string lowered = value;
			std::transform(lowered.begin(), lowered.end(), lowered.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			return lowered.find(token) != std::string::npos;
		}

	}

	connection::connection(std::shared_ptr<io::stream> wire, const params& p, async::io_context& ctx, handler_type handler)
		: wire_(std::move(wire)), params_(p), io_ctx_(ctx), handler_(std::move(handler))
	{
	}

	async::task<bool> connection::fill_more() {
		char chunk[4096];
		const std::size_t n = co_await wire_->read(chunk, sizeof(chunk));

		if (n == 0)
			co_return false;

		read_buffer_.append(chunk, n);
		co_return true;
	}

	async::task<bool> connection::read_request_line(protocol::http_resource& out) {
		for (;;) {
			const std::ptrdiff_t consumed = protocol::http_resource::try_parse(read_buffer_.data(), read_buffer_.size(), out);

			if (consumed > 0) {
				read_buffer_.erase(0, static_cast<std::size_t>(consumed));
				co_return true;
			}

			if (consumed < 0)
				co_return false;

			if (read_buffer_.size() > params_.max_request_line_size)
				co_return false;

			if (!co_await fill_more())
				co_return false;
		}
	}

	async::task<bool> connection::read_headers(protocol::http_headers& out) {
		for (;;) {
			std::size_t nl = read_buffer_.find('\n');

			while (nl == std::string::npos) {
				if (read_buffer_.size() > params_.max_header_size)
					co_return false;

				if (!co_await fill_more())
					co_return false;

				nl = read_buffer_.find('\n');
			}

			std::size_t line_len = nl;

			if (line_len > 0 && read_buffer_[line_len - 1] == '\r')
				--line_len;

			if (line_len == 0) {
				read_buffer_.erase(0, nl + 1);
				co_return true;
			}

			protocol::http_header h;
			const std::ptrdiff_t consumed = protocol::http_header::try_parse(read_buffer_.data(), read_buffer_.size(), h);

			if (consumed <= 0)
				co_return false;

			out.add(std::move(h.name), std::move(h.value));
			read_buffer_.erase(0, static_cast<std::size_t>(consumed));
		}
	}

	async::task<std::shared_ptr<io::stream>> connection::make_body_stream(const protocol::http_headers& headers) {
		auto source = std::make_shared<remaining_wire_stream>(read_buffer_, *wire_);

		if (const std::string* te = headers.get(protocol::header_names::TRANSFER_ENCODING)) {
			if (header_value_contains_token(*te, "chunked"))
				co_return std::make_shared<protocol::chunked_decoder_stream>(std::move(source));
		}

		if (const std::string* cl = headers.get(protocol::header_names::CONTENT_LENGTH)) {
			std::int64_t length = 0;

			for (const char c : *cl) {
				if (c < '0' || c > '9')
					co_return nullptr; // malformed Content-Length

				length = length * 10 + (c - '0');
			}

			co_return std::make_shared<io::range_stream>(std::move(source), 0, length);
		}

		co_return std::make_shared<io::memory_stream>();
	}

	std::string connection::extract_hostname(const protocol::http_headers& headers) {
		const std::string* host = headers.get(protocol::header_names::HOST);

		if (!host)
			return std::string();

		std::string_view v(*host);

		if (!v.empty() && v.front() == '[') {
			// "[::1]:8080" style — keep the bracketed literal, drop any trailing port.
			const std::size_t close = v.find(']');

			if (close != std::string_view::npos)
				return std::string(v.substr(0, close + 1));

			return std::string(v);
		}

		const std::size_t colon = v.find(':');
		return std::string(colon == std::string_view::npos ? v : v.substr(0, colon));
	}

	bool connection::wants_keep_alive(const protocol::http_resource& resource, const protocol::http_headers& headers) {
		if (const std::string* conn = headers.get(protocol::header_names::CONNECTION)) {
			if (header_value_contains_token(*conn, "close"))
				return false;

			if (header_value_contains_token(*conn, "keep-alive"))
				return true;
		}

		return resource.http_minor >= 1;
	}

	async::task<void> connection::write_all(const void* buf, std::size_t n) {
		const char* p = static_cast<const char*>(buf);
		std::size_t written = 0;

		while (written < n)
			written += co_await wire_->write(p + written, n - written);
	}

	async::task<void> connection::write_response(response& resp, bool keep_alive) {
		if (resp.upgrade_handler) {
			// a protocol upgrade (e.g. WebSocket): send exactly the status line
			// and headers the extension set, nothing more — then hand the raw
			// wire (plus anything already buffered past it) off entirely, and
			// stop being an HTTP connection.
			std::string head;
			resp.status.write_status_line(head, 1);
			resp.headers.write_to(head);
			head += "\r\n";

			co_await write_all(head.data(), head.size());

			std::string leftover = std::move(read_buffer_);

			upgraded_ = true;
			co_await resp.upgrade_handler(std::move(wire_), std::move(leftover));
			co_return;
		}

		if (!params_.server_header_value.empty() && !resp.headers.isset(protocol::header_names::SERVER))
			resp.headers.set(std::string(protocol::header_names::SERVER), params_.server_header_value);

		resp.headers.set(std::string(protocol::header_names::DATE), protocol::format_http_date(std::time(nullptr)));
		resp.headers.set(std::string(protocol::header_names::CONNECTION), keep_alive ? "keep-alive" : "close");

		const bool use_chunked = resp.body && resp.content_length < 0;

		if (use_chunked)
			resp.headers.set(std::string(protocol::header_names::TRANSFER_ENCODING), "chunked");
		else
			resp.headers.set(std::string(protocol::header_names::CONTENT_LENGTH), std::to_string(std::max<std::int64_t>(0, resp.content_length)));

		std::string head;
		resp.status.write_status_line(head, 1);
		resp.headers.write_to(head);
		head += "\r\n";

		co_await write_all(head.data(), head.size());

		if (!resp.body)
			co_return;

		char buf[4096];

		if (use_chunked) {
			for (;;) {
				const std::size_t got = co_await resp.body->read(buf, sizeof(buf));

				if (got == 0)
					break;

				const std::string chunk_head = protocol::format_chunk_header(got);
				co_await write_all(chunk_head.data(), chunk_head.size());
				co_await write_all(buf, got);
				co_await write_all(protocol::chunk_data_terminator.data(), protocol::chunk_data_terminator.size());
			}

			co_await write_all(protocol::chunked_body_terminator.data(), protocol::chunked_body_terminator.size());
		}
		else {
			std::int64_t remaining = resp.content_length;

			while (remaining > 0) {
				const std::size_t want = static_cast<std::size_t>(std::min<std::int64_t>(remaining, static_cast<std::int64_t>(sizeof(buf))));
				const std::size_t got = co_await resp.body->read(buf, want);

				if (got == 0)
					break;

				co_await write_all(buf, got);
				remaining -= static_cast<std::int64_t>(got);
			}
		}
	}

	async::task<void> connection::run() {
		for (;;) {
			request req;
			req.io_ctx = &io_ctx_;

			if (!co_await read_request_line(req.resource))
				co_return;

			if (!co_await read_headers(req.headers)) {
				response bad = make_response(400);
				co_await write_response(bad, false);
				co_return;
			}

			req.hostname = extract_hostname(req.headers);
			req.body = co_await make_body_stream(req.headers);

			if (!req.body) {
				response bad = make_response(400);
				co_await write_response(bad, false);
				co_return;
			}

			response resp = handler_ ? co_await handler_(req) : make_response(501);

			// drain any unread request body so its bytes don't get mistaken for
			// the start of the next request on this (possibly keep-alive) connection.
			char scratch[4096];
			while (co_await req.body->read(scratch, sizeof(scratch)) != 0) {
			}

			const bool keep_alive = wants_keep_alive(req.resource, req.headers);
			co_await write_response(resp, keep_alive);

			if (upgraded_ || !keep_alive)
				co_return;
		}
	}

}
