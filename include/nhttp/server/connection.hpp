#pragma once

#include "params.hpp"
#include "request.hpp"
#include "response.hpp"
#include "tag_storage.hpp"
#include "../async/task.hpp"
#include "../io/stream.hpp"

#include <functional>
#include <string>

namespace nhttp::server {

	using handler_type = std::function<async::task<response>(request&)>;

	/**
	 * class connection.
	 * one HTTP/1.1 connection's read-dispatch-write loop, expressed as a single
	 * coroutine — this replaces the original implementation's
	 * PREPARING/RECEIVE_REQUEST/.../RESETTING state-machine enum entirely (see
	 * CONCEPTS.md §2): the coroutine's suspend points *are* the state machine.
	 */
	class connection {
	public:
		/* `wire` is transport-agnostic on purpose (CONCEPTS.md's seam for a
		 * future non-TCP transport, e.g. QUIC): anything implementing io::stream
		 * works, not just a TCP socket_stream. `ctx` is the io_context actually
		 * running this connection (stamped onto every request as request::io_ctx
		 * — see its doc comment for why this must not be a different context). */
		connection(std::shared_ptr<io::stream> wire, const params& p, async::io_context& ctx, handler_type handler);

	public:
		/* runs until the peer closes, a protocol error occurs, or keep-alive ends. */
		async::task<void> run();

	public:
		tag_storage& tags() noexcept { return connection_tags_; }

	private:
		async::task<bool> fill_more();
		async::task<bool> read_request_line(protocol::http_resource& out);
		async::task<bool> read_headers(protocol::http_headers& out);
		async::task<std::shared_ptr<io::stream>> make_body_stream(const protocol::http_headers& headers);
		async::task<void> write_all(const void* buf, std::size_t n);
		async::task<void> write_response(response& resp, bool keep_alive);

		static std::string extract_hostname(const protocol::http_headers& headers);
		static bool wants_keep_alive(const protocol::http_resource& resource, const protocol::http_headers& headers);

	private:
		std::shared_ptr<io::stream> wire_;
		params params_;
		async::io_context& io_ctx_;
		handler_type handler_;
		std::string read_buffer_;
		tag_storage connection_tags_;
		bool upgraded_ = false;
	};

}
