#include "nhttp/server/connection.hpp"
#include "nhttp/server/http1_io.hpp"
#include "nhttp/protocol/http_date.hpp"
#include "nhttp/protocol/urlencode.hpp"

#include <ctime>

namespace nhttp::server {

	connection::connection(std::shared_ptr<io::stream> wire, const params& p, async::io_context& ctx, handler_type handler,
		std::string initial_buffer)
		: wire_(std::move(wire)), params_(p), io_ctx_(ctx), handler_(std::move(handler)), read_buffer_(std::move(initial_buffer))
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
			if (protocol::header_value_contains_token(*conn, "close"))
				return false;

			if (protocol::header_value_contains_token(*conn, "keep-alive"))
				return true;
		}

		return resource.http_minor >= 1;
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

			co_await http1_io::write_all(*wire_, head.data(), head.size());

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

		co_await http1_io::write_all(*wire_, head.data(), head.size());

		if (!resp.body)
			co_return;

		co_await http1_io::write_message_body(*wire_, *resp.body, use_chunked ? -1 : resp.content_length);
	}

	async::task<void> connection::run() {
		for (;;) {
			request req;
			req.io_ctx = &io_ctx_;

			if (!co_await read_request_line(req.resource))
				co_return;

			if (!co_await http1_io::read_headers(read_buffer_, *wire_, req.headers, params_.max_header_size)) {
				response bad = make_response(400);
				co_await write_response(bad, false);
				co_return;
			}

			req.hostname = extract_hostname(req.headers);
			req.body = http1_io::make_body_stream(read_buffer_, wire_, req.headers);

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
