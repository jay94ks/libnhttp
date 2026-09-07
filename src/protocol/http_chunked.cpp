#include "nhttp/protocol/http_chunked.hpp"

#include <algorithm>
#include <cstring>

namespace nhttp::protocol {

	std::string format_chunk_header(std::size_t length) {
		static constexpr char digits[] = "0123456789abcdef";

		if (length == 0)
			return "0\r\n";

		std::string hex;
		std::size_t v = length;

		while (v > 0) {
			hex += digits[v & 0xF];
			v >>= 4;
		}

		std::reverse(hex.begin(), hex.end());
		hex += "\r\n";
		return hex;
	}

	chunked_decoder_stream::chunked_decoder_stream(std::shared_ptr<io::stream> source)
		: source_(std::move(source))
	{
	}

	async::task<bool> chunked_decoder_stream::fill_more() {
		if (eof_source_)
			co_return false;

		char chunk[4096];
		const std::size_t n = co_await source_->read(chunk, sizeof(chunk));

		if (n == 0) {
			eof_source_ = true;
			co_return false;
		}

		buffer_.append(chunk, n);
		co_return true;
	}

	async::task<bool> chunked_decoder_stream::read_line(std::string& out) {
		for (;;) {
			const std::size_t nl = buffer_.find('\n');

			if (nl != std::string::npos) {
				std::size_t len = nl;

				if (len > 0 && buffer_[len - 1] == '\r')
					--len;

				out.assign(buffer_, 0, len);
				buffer_.erase(0, nl + 1);
				co_return true;
			}

			if (eof_source_)
				co_return false;

			co_await fill_more();
		}
	}

	async::task<bool> chunked_decoder_stream::begin_next_chunk() {
		std::string line;

		if (!co_await read_line(line)) {
			finished_ = true;
			co_return false;
		}

		std::size_t size = 0;
		bool any_digit = false;

		for (const char c : line) {
			int v;

			if (c >= '0' && c <= '9') v = c - '0';
			else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
			else break; // chunk-extension or stray whitespace: stop reading digits.

			size = size * 16 + static_cast<std::size_t>(v);
			any_digit = true;
		}

		if (!any_digit) {
			finished_ = true;
			co_return false;
		}

		if (size == 0) {
			for (;;) {
				std::string trailer_line;

				if (!co_await read_line(trailer_line) || trailer_line.empty())
					break;
			}

			finished_ = true;
			co_return false;
		}

		remaining_in_chunk_ = size;
		need_new_chunk_ = false;
		co_return true;
	}

	async::task<std::size_t> chunked_decoder_stream::read(void* buf, std::size_t n) {
		for (;;) {
			if (finished_)
				co_return 0;

			if (need_new_chunk_) {
				if (!co_await begin_next_chunk())
					co_return 0;
			}

			while (buffer_.empty() && !eof_source_)
				co_await fill_more();

			if (buffer_.empty()) {
				// source ended mid-chunk: malformed body, treat as end of stream.
				finished_ = true;
				co_return 0;
			}

			const std::size_t to_copy = std::min({ n, remaining_in_chunk_, buffer_.size() });

			if (to_copy == 0) {
				need_new_chunk_ = true;
				continue;
			}

			std::memcpy(buf, buffer_.data(), to_copy);
			buffer_.erase(0, to_copy);
			remaining_in_chunk_ -= to_copy;

			if (remaining_in_chunk_ == 0) {
				while (buffer_.size() < 2 && !eof_source_)
					co_await fill_more();

				if (buffer_.size() >= 2)
					buffer_.erase(0, 2);

				need_new_chunk_ = true;
			}

			co_return to_copy;
		}
	}

	async::task<std::int64_t> chunked_decoder_stream::seek(std::int64_t, io::seek_origin) {
		co_return -1;
	}

	async::task<std::size_t> chunked_decoder_stream::write(const void*, std::size_t) {
		co_return 0;
	}

	async::task<void> chunked_decoder_stream::flush() {
		co_return;
	}

	async::task<void> chunked_decoder_stream::close() {
		co_return;
	}

}
