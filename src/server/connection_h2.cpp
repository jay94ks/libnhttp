#include "nhttp/server/connection_h2.hpp"
#include "nhttp/protocol/urlencode.hpp"
#include "nhttp/io/memory_stream.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace nhttp::server {

	namespace {

		bool is_h2_forbidden_header(std::string_view name) noexcept {
			// RFC 9113 §8.2.2: connection-specific header fields must not
			// appear in an HTTP/2 message — these are the ones any response
			// built against this library's (HTTP/1.1-shaped) response API
			// could plausibly still be carrying.
			static constexpr std::string_view forbidden[] = {
				"Connection", "Keep-Alive", "Proxy-Connection", "Transfer-Encoding", "Upgrade",
			};

			for (const std::string_view f : forbidden) {
				if (protocol::header_name_equals(name, f))
					return true;
			}

			return false;
		}

		std::string to_lower(std::string_view s) {
			std::string out(s);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		/* suspends the calling coroutine until handle_window_update resumes it
		 * (by writing a non-null handle into *slot and calling .resume()). */
		struct window_wait_awaiter {
			std::coroutine_handle<>* slot;

			bool await_ready() const noexcept { return false; }
			void await_suspend(std::coroutine_handle<> h) const noexcept { *slot = h; }
			void await_resume() const noexcept { }
		};

	}

	bool connection_h2::writer_lock::awaiter::await_suspend(std::coroutine_handle<> h) {
		if (!self.locked_) {
			self.locked_ = true;
			return false; // C++20: returning false from a bool await_suspend resumes immediately, no real suspend.
		}

		self.waiters_.push_back(h);
		return true;
	}

	void connection_h2::writer_lock::unlock() {
		if (!waiters_.empty()) {
			const std::coroutine_handle<> h = waiters_.front();
			waiters_.erase(waiters_.begin());
			h.resume();
		}
		else {
			locked_ = false;
		}
	}

	connection_h2::connection_h2(std::shared_ptr<io::stream> wire, const params& p, async::io_context& ctx,
		handler_type handler, std::string preface_leftover)
		: wire_(std::move(wire)), params_(p), io_ctx_(ctx), handler_(std::move(handler)),
		  read_buffer_(std::move(preface_leftover)), max_request_body_size_(16u * 1024 * 1024)
	{
	}

	async::task<bool> connection_h2::fill_more() {
		char chunk[4096];
		const std::size_t n = co_await wire_->read(chunk, sizeof(chunk));

		if (n == 0)
			co_return false;

		read_buffer_.append(chunk, n);
		co_return true;
	}

	async::task<bool> connection_h2::read_exact(void* buf, std::size_t n) {
		while (read_buffer_.size() < n) {
			if (!co_await fill_more())
				co_return false;
		}

		std::memcpy(buf, read_buffer_.data(), n);
		read_buffer_.erase(0, n);
		co_return true;
	}

	async::task<bool> connection_h2::read_frame_header(http2::frame_header& out) {
		std::uint8_t buf[http2::frame_header::wire_size];

		if (!co_await read_exact(buf, sizeof(buf)))
			co_return false;

		http2::frame_header::parse(buf, out);
		co_return true;
	}

	async::task<void> connection_h2::send_frame(http2::frame_type type, std::uint8_t flags, std::uint32_t stream_id, std::string_view payload) {
		co_await write_lock_.lock();

		http2::frame_header fh;
		fh.length = static_cast<std::uint32_t>(payload.size());
		fh.type = type;
		fh.flags = flags;
		fh.stream_id = stream_id;

		std::string out;
		fh.write(out);
		out += payload;

		std::size_t written = 0;

		while (written < out.size())
			written += co_await wire_->write(out.data() + written, out.size() - written);

		write_lock_.unlock();
	}

	async::task<void> connection_h2::send_settings_ack() {
		co_await send_frame(http2::frame_type::settings, http2::frame_flags::ack, 0, std::string_view());
	}

	async::task<void> connection_h2::send_goaway(std::uint32_t error) {
		std::string payload;
		http2::write_goaway_payload(payload, highest_stream_id_, error, std::string_view());
		co_await send_frame(http2::frame_type::goaway, 0, 0, payload);
	}

	async::task<bool> connection_h2::handle_settings(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload) {
		if (fh.stream_id != 0)
			co_return false;

		if (fh.flags & http2::frame_flags::ack)
			co_return true; // acknowledges our own SETTINGS; nothing to do.

		std::vector<http2::settings_param> parsed;

		if (!http2::parse_settings_payload(payload.data(), payload.size(), parsed))
			co_return false;

		for (const http2::settings_param& p : parsed) {
			if (p.id == http2::settings_id::initial_window_size)
				peer_initial_window_size_ = p.value;
			else if (p.id == http2::settings_id::max_frame_size)
				peer_max_frame_size_ = p.value == 0 ? peer_max_frame_size_ : p.value;
			// header_table_size/enable_push/max_concurrent_streams/
			// max_header_list_size: intentionally not enforced in this pass
			// (see the class doc comment) — header_table_size in particular
			// has no effect on hpack_encoder_ here, since it never uses the
			// dynamic table at all (always literal-without-indexing).
		}

		co_await send_settings_ack();
		co_return true;
	}

	async::task<bool> connection_h2::handle_ping(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload) {
		if (fh.stream_id != 0 || payload.size() != 8)
			co_return false;

		if (fh.flags & http2::frame_flags::ack)
			co_return true;

		co_await send_frame(http2::frame_type::ping, http2::frame_flags::ack, 0,
			std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
		co_return true;
	}

	void connection_h2::finish_header_block(std::shared_ptr<h2_stream> stream) {
		http2::header_list fields;

		if (!hpack_decoder_.decode(reinterpret_cast<const std::uint8_t*>(stream->header_block.data()), stream->header_block.size(), fields)) {
			stream->malformed = true;
			return;
		}

		for (http2::header_field& f : fields) {
			if (f.name == ":method") {
				stream->resource.method = protocol::http_method(std::move(f.value));
			}
			else if (f.name == ":authority") {
				stream->hostname = std::move(f.value);
			}
			else if (f.name == ":path") {
				const std::size_t qpos = f.value.find('?');

				if (qpos == std::string::npos) {
					stream->resource.raw_path = f.value;
					stream->resource.query = protocol::http_query_string::parse(std::string_view());
				}
				else {
					stream->resource.raw_path = f.value.substr(0, qpos);
					stream->resource.query = protocol::http_query_string::parse(std::string_view(f.value).substr(qpos + 1));
				}

				stream->resource.path = protocol::url_decode(stream->resource.raw_path);
			}
			else if (f.name == ":scheme") {
				// not modeled anywhere in http_resource/request today — a
				// reverse_proxy relaying an h2 request onward would need
				// this; out of scope for this pass (see class doc comment).
			}
			else if (!f.name.empty() && f.name[0] == ':') {
				// unrecognized pseudo-header — ignore leniently rather than
				// failing the whole stream.
			}
			else {
				stream->headers.add(std::move(f.name), std::move(f.value));
			}
		}

		stream->resource.http_major = 2;
		stream->resource.http_minor = 0;
	}

	bool connection_h2::handle_headers(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload) {
		if (fh.stream_id == 0 || expecting_continuation_)
			return false;

		const bool padded = (fh.flags & http2::frame_flags::padded) != 0;
		const bool has_priority = (fh.flags & http2::frame_flags::priority) != 0;
		const bool end_headers = (fh.flags & http2::frame_flags::end_headers) != 0;
		const bool end_stream = (fh.flags & http2::frame_flags::end_stream) != 0;

		std::size_t content_len = 0, content_offset = 0;

		if (!http2::strip_padding(payload.data(), payload.size(), padded, content_len, content_offset))
			return false;

		if (has_priority) {
			if (content_len < 5)
				return false;

			content_offset += 5; // stream dependency (4 bytes) + weight (1 byte) — ignored, see class doc comment.
			content_len -= 5;
		}

		auto stream = std::make_shared<h2_stream>();
		stream->id = fh.stream_id;
		stream->send_window = static_cast<std::int64_t>(peer_initial_window_size_);
		stream->header_block.assign(reinterpret_cast<const char*>(payload.data() + content_offset), content_len);
		stream->end_stream = end_stream;

		streams_[fh.stream_id] = stream;
		highest_stream_id_ = std::max(highest_stream_id_, fh.stream_id);

		if (end_headers) {
			finish_header_block(stream);
			stream->end_headers = true;

			if (stream->end_stream || stream->malformed)
				dispatch_stream(stream);
		}
		else {
			expecting_continuation_ = true;
			last_header_stream_id_ = fh.stream_id;
		}

		return true;
	}

	bool connection_h2::handle_continuation(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload) {
		if (!expecting_continuation_ || fh.stream_id != last_header_stream_id_)
			return false;

		const auto it = streams_.find(fh.stream_id);

		if (it == streams_.end())
			return false;

		std::shared_ptr<h2_stream> stream = it->second;
		stream->header_block.append(reinterpret_cast<const char*>(payload.data()), payload.size());

		if (fh.flags & http2::frame_flags::end_headers) {
			expecting_continuation_ = false;
			finish_header_block(stream);
			stream->end_headers = true;

			if (stream->end_stream || stream->malformed)
				dispatch_stream(stream);
		}

		return true;
	}

	bool connection_h2::handle_window_update(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload) {
		std::uint32_t increment = 0;

		if (!http2::parse_window_update_payload(payload.data(), payload.size(), increment) || increment == 0)
			return false;

		if (fh.stream_id == 0) {
			connection_send_window_ += increment;

			std::vector<std::coroutine_handle<>> to_resume;

			for (auto& [id, s] : streams_) {
				if (s->window_waiter && s->send_window > 0) {
					to_resume.push_back(s->window_waiter);
					s->window_waiter = nullptr;
				}
			}

			for (const std::coroutine_handle<>& h : to_resume)
				h.resume();
		}
		else {
			const auto it = streams_.find(fh.stream_id);

			if (it != streams_.end()) {
				it->second->send_window += increment;

				if (it->second->window_waiter) {
					const std::coroutine_handle<> h = it->second->window_waiter;
					it->second->window_waiter = nullptr;
					h.resume();
				}
			}
		}

		return true;
	}

	async::task<void> connection_h2::run() {
		std::string preface_check;
		preface_check.resize(http2::client_preface.size());

		if (!co_await read_exact(preface_check.data(), preface_check.size()) || preface_check != http2::client_preface)
			co_return;

		co_await send_frame(http2::frame_type::settings, 0, 0, std::string_view());

		for (;;) {
			http2::frame_header fh;

			if (!co_await read_frame_header(fh))
				co_return;

			if (fh.length > 16u * 1024 * 1024)
				co_return; // sanity cap, far above any header/data frame this server expects.

			std::vector<std::uint8_t> payload(fh.length);

			if (fh.length > 0 && !co_await read_exact(payload.data(), fh.length))
				co_return;

			bool ok = true;

			switch (fh.type) {
			case http2::frame_type::settings: ok = co_await handle_settings(fh, payload); break;
			case http2::frame_type::headers: ok = handle_headers(fh, payload); break;
			case http2::frame_type::continuation: ok = handle_continuation(fh, payload); break;
			case http2::frame_type::data: ok = co_await handle_data(fh, payload); break;
			case http2::frame_type::window_update: ok = handle_window_update(fh, payload); break;
			case http2::frame_type::rst_stream: streams_.erase(fh.stream_id); break;
			case http2::frame_type::ping: ok = co_await handle_ping(fh, payload); break;
			case http2::frame_type::goaway: co_return;
			case http2::frame_type::priority: break; // parsed nowhere; 5-byte payload carries no state we keep.
			default: break; // unknown frame type — RFC 9113 §4.1: ignore and discard.
			}

			if (!ok) {
				co_await send_goaway(http2::error_code::protocol_error);
				co_return;
			}
		}
	}

	async::task<bool> connection_h2::handle_data(const http2::frame_header& fh, const std::vector<std::uint8_t>& payload) {
		const auto it = streams_.find(fh.stream_id);

		if (it == streams_.end())
			co_return true; // unknown/already-closed stream — ignore leniently.

		std::shared_ptr<h2_stream> stream = it->second;
		const bool padded = (fh.flags & http2::frame_flags::padded) != 0;
		std::size_t content_len = 0, content_offset = 0;

		if (!http2::strip_padding(payload.data(), payload.size(), padded, content_len, content_offset))
			co_return false;

		if (stream->body_buffer.size() + content_len > max_request_body_size_)
			co_return false;

		stream->body_buffer.append(reinterpret_cast<const char*>(payload.data() + content_offset), content_len);

		if (fh.length > 0) {
			// replenish both windows by exactly what was consumed — simple
			// and correct, if not the most bandwidth-efficient possible
			// approach (a real implementation might batch these).
			std::string wu;
			http2::write_window_update_payload(wu, fh.length);
			co_await send_frame(http2::frame_type::window_update, 0, fh.stream_id, wu);
			co_await send_frame(http2::frame_type::window_update, 0, 0, wu);
		}

		if (fh.flags & http2::frame_flags::end_stream) {
			stream->end_stream = true;

			if (stream->end_headers)
				dispatch_stream(stream);
		}

		co_return true;
	}

	async::detached_task connection_h2::dispatch_stream(std::shared_ptr<h2_stream> stream) {
		try {
			response resp;

			if (stream->malformed) {
				resp = make_response(400);
			}
			else {
				request req;
				req.io_ctx = &io_ctx_;
				req.resource = stream->resource;
				req.headers = std::move(stream->headers);
				req.hostname = stream->hostname;
				req.body = std::make_shared<io::memory_stream>(
					std::vector<std::uint8_t>(stream->body_buffer.begin(), stream->body_buffer.end()));

				resp = handler_ ? co_await handler_(req) : make_response(501);
			}

			co_await write_response(stream, std::move(resp));
		}
		catch (...) {
			// a single stream's failure must never take down the connection.
		}

		streams_.erase(stream->id);
	}

	async::task<void> connection_h2::write_response(std::shared_ptr<h2_stream> stream, response resp) {
		if (resp.upgrade_handler) {
			// RFC 9113 §8.5: the HTTP/1.1 Upgrade mechanism doesn't apply to
			// HTTP/2 at all — an extension that set this (e.g. a WebSocket
			// endpoint) just gets a 501 over h2, rather than silently hanging.
			resp = make_response(501);
		}

		http2::header_list fields;
		fields.push_back({ ":status", std::to_string(resp.status.code()) });

		for (const protocol::http_header& h : resp.headers) {
			if (!is_h2_forbidden_header(h.name))
				fields.push_back({ to_lower(h.name), h.value });
		}

		std::string header_block;
		hpack_encoder_.encode(fields, header_block);

		const bool has_body = static_cast<bool>(resp.body) && resp.content_length != 0;
		std::uint8_t flags = http2::frame_flags::end_headers;

		if (!has_body)
			flags = static_cast<std::uint8_t>(flags | http2::frame_flags::end_stream);

		co_await send_frame(http2::frame_type::headers, flags, stream->id, header_block);

		if (has_body)
			co_await write_data_frames(stream, resp.body, resp.content_length);
	}

	async::task<void> connection_h2::write_data_frames(std::shared_ptr<h2_stream> stream, std::shared_ptr<io::stream> body, std::int64_t content_length) {
		char buf[16384];
		std::int64_t remaining = content_length; // negative = unknown length, read until EOF.

		for (;;) {
			std::size_t want = sizeof(buf);

			if (remaining >= 0) {
				if (remaining == 0)
					break;

				want = static_cast<std::size_t>(std::min<std::int64_t>(remaining, static_cast<std::int64_t>(sizeof(buf))));
			}

			const std::size_t got = co_await body->read(buf, want);

			if (got == 0)
				break;

			std::size_t sent_from_chunk = 0;

			while (sent_from_chunk < got) {
				while (stream->send_window <= 0 || connection_send_window_ <= 0)
					co_await window_wait_awaiter{ &stream->window_waiter };

				const std::int64_t allowed = std::min({
					stream->send_window,
					connection_send_window_,
					static_cast<std::int64_t>(peer_max_frame_size_),
					static_cast<std::int64_t>(got - sent_from_chunk) });

				const auto n = static_cast<std::size_t>(allowed);
				co_await send_frame(http2::frame_type::data, 0, stream->id, std::string_view(buf + sent_from_chunk, n));
				stream->send_window -= allowed;
				connection_send_window_ -= allowed;
				sent_from_chunk += n;
			}

			if (remaining >= 0)
				remaining -= static_cast<std::int64_t>(got);
		}

		co_await send_frame(http2::frame_type::data, http2::frame_flags::end_stream, stream->id, std::string_view());
	}

}
