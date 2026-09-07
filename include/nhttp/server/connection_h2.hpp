#pragma once

#include "connection.hpp"
#include "../http2/frame.hpp"
#include "../http2/hpack.hpp"

#include <coroutine>
#include <cstdint>
#include <unordered_map>

namespace nhttp::server {

	/**
	 * class connection_h2.
	 * one HTTP/2 connection's frame-dispatch loop — the "different connection
	 * type, same downstream machinery" seam docs/protocol-extensibility.md's
	 * Seam-1 review verified holds: this decodes a multiplexed frame layer
	 * into many concurrent request/response exchanges, each run through the
	 * *same* dispatch callback (listener::dispatch) the HTTP/1.1 `connection`
	 * uses, dispatched one detached coroutine per stream.
	 *
	 * known simplifications, in the same spirit as earlier phases' logged
	 * ones: a request body is fully buffered before its handler runs (no
	 * live incremental request-body streaming — simpler, at the cost of
	 * holding a large upload fully in memory); server push, the legacy
	 * `Upgrade: h2c` bootstrap, and PRIORITY frame reordering are not
	 * implemented (push is deprecated in practice; h2c's prior-knowledge
	 * bootstrap, which *is* supported, is what virtually every current
	 * client/tool actually uses; PRIORITY is parsed and ignored, matching
	 * RFC 9113's own de-emphasis of the original priority scheme); a
	 * response's headers are assumed to fit in one HEADERS frame (no
	 * CONTINUATION on the send side — real header sets are essentially
	 * always well under SETTINGS_MAX_FRAME_SIZE).
	 */
	class connection_h2 {
	public:
		/* `preface_leftover` is any bytes already read past the client
		 * connection preface (RFC 9113 §3.4) by whoever detected this was an
		 * HTTP/2 connection (listener's prior-knowledge peek, or the ALPN
		 * path where the preface hasn't been read at all yet — pass an empty
		 * string there). */
		connection_h2(std::shared_ptr<io::stream> wire, const params& p, async::io_context& ctx,
			handler_type handler, std::string preface_leftover);

		async::task<void> run();

	private:
		struct h2_stream {
			std::uint32_t id = 0;
			protocol::http_resource resource;
			protocol::http_headers headers;
			std::string hostname;
			std::string header_block; // accumulated across CONTINUATION until END_HEADERS
			std::string body_buffer;  // fully-buffered request body (see class doc comment)
			bool end_headers = false;
			bool end_stream = false;
			bool malformed = false;
			std::int64_t send_window = 65535;
			std::coroutine_handle<> window_waiter;
		};

		// a minimal single-threaded (single-io_context) cooperative lock —
		// see .cpp for why a real (thread-safe) mutex isn't needed here.
		class writer_lock {
		public:
			struct awaiter {
				writer_lock& self;
				bool await_ready() const noexcept { return false; }
				bool await_suspend(std::coroutine_handle<> h);
				void await_resume() const noexcept { }
			};

			awaiter lock() noexcept { return awaiter{ *this }; }
			void unlock();

		private:
			bool locked_ = false;
			std::vector<std::coroutine_handle<>> waiters_;
		};

		async::task<bool> fill_more();
		async::task<bool> read_exact(void* buf, std::size_t n);
		async::task<bool> read_frame_header(http2::frame_header& out);

		async::task<void> send_frame(http2::frame_type type, std::uint8_t flags, std::uint32_t stream_id, std::string_view payload);
		async::task<void> send_settings_ack();
		async::task<void> send_goaway(std::uint32_t error);

		async::task<bool> handle_settings(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload);
		async::task<bool> handle_ping(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload);
		bool handle_headers(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload);
		bool handle_continuation(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload);
		async::task<bool> handle_data(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload);
		bool handle_window_update(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload);

		void finish_header_block(std::shared_ptr<h2_stream> stream);
		async::detached_task dispatch_stream(std::shared_ptr<h2_stream> stream);
		async::task<void> write_response(std::shared_ptr<h2_stream> stream, response resp);
		async::task<void> write_data_frames(std::shared_ptr<h2_stream> stream, std::shared_ptr<io::stream> body, std::int64_t content_length);

		std::shared_ptr<io::stream> wire_;
		params params_;
		async::io_context& io_ctx_;
		handler_type handler_;
		std::string read_buffer_;

		http2::hpack_decoder hpack_decoder_;
		http2::hpack_encoder hpack_encoder_;
		writer_lock write_lock_;

		std::unordered_map<std::uint32_t, std::shared_ptr<h2_stream>> streams_;
		std::uint32_t highest_stream_id_ = 0;
		std::uint32_t last_header_stream_id_ = 0; // for validating CONTINUATION follows the right HEADERS
		bool expecting_continuation_ = false;

		std::int64_t connection_send_window_ = 65535;
		std::int64_t connection_recv_window_ = 65535;
		std::uint32_t peer_initial_window_size_ = 65535;
		std::uint32_t peer_max_frame_size_ = 16384;
		std::size_t max_request_body_size_;
	};

}
