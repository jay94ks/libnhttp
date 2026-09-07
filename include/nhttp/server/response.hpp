#pragma once

#include "../protocol/http_status.hpp"
#include "../protocol/http_header.hpp"
#include "../protocol/http_mime_type.hpp"
#include "../io/stream.hpp"
#include "../async/task.hpp"

#include <functional>
#include <memory>
#include <string>

namespace nhttp::server {

	/**
	 * class response.
	 * what a handler builds and returns. `content_length < 0` means "unknown" —
	 * the connection will send it with chunked transfer-coding; otherwise it's
	 * sent with an explicit Content-Length, even if that differs from
	 * `body->get_length()` (the explicit value always wins).
	 */
	class response {
	public:
		protocol::http_status status{ 200 };
		protocol::http_headers headers;
		std::shared_ptr<io::stream> body;
		std::int64_t content_length = 0;

		/**
		 * set this (instead of body/content_length) for a protocol upgrade
		 * (e.g. WebSocket): connection::write_response sends only the status
		 * line + headers set here (no Content-Length/Transfer-Encoding/
		 * Connection are added automatically), then calls this with the raw
		 * connection stream and any bytes already read past it, and ends its
		 * own HTTP loop once it returns — see CONCEPTS.md's protocol-upgrade
		 * design and ws/connection.hpp.
		 */
		std::function<async::task<void>(std::shared_ptr<io::stream>, std::string)> upgrade_handler;
	};

	/* status-only response, e.g. make_response(404). */
	response make_response(int status_code);

	/* 200 OK with a text body (defaults to text/html, matching the historical default). */
	response make_response(std::string text, std::string_view mime = protocol::mime_types::TEXT_HTML);

	/* 200 OK streaming an arbitrary body; length < 0 uses body->get_length(), and
	 * if that's also unknown, the response is sent chunked. */
	response make_response(std::shared_ptr<io::stream> body, std::string_view mime, std::int64_t length = -1);

}
