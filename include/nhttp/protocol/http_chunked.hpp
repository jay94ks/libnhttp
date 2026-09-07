#pragma once

#include "../io/stream.hpp"

#include <memory>
#include <string>

namespace nhttp::protocol {

	/* formats one chunk's size-line, e.g. "1a\r\n" for a 26-byte chunk. */
	std::string format_chunk_header(std::size_t length);

	/* the terminating sequence for a chunk's data ("\r\n") and for the whole body. */
	inline constexpr std::string_view chunk_data_terminator = "\r\n";
	inline constexpr std::string_view chunked_body_terminator = "0\r\n\r\n";

	/**
	 * class chunked_decoder_stream.
	 * decodes an HTTP/1.1 chunked-transfer-coded body read from `source` (the raw
	 * connection stream) into a plain byte stream. chunk extensions are ignored;
	 * trailing headers after the final "0" chunk are consumed and discarded.
	 */
	class chunked_decoder_stream final : public io::stream {
	public:
		explicit chunked_decoder_stream(std::shared_ptr<io::stream> source);

	public:
		std::int64_t get_length() const override { return -1; }
		bool can_seek() const noexcept override { return false; }

		async::task<std::int64_t> seek(std::int64_t offset, io::seek_origin origin) override;
		async::task<std::size_t> read(void* buf, std::size_t n) override;
		async::task<std::size_t> write(const void* buf, std::size_t n) override;
		async::task<void> flush() override;
		async::task<void> close() override;

	private:
		async::task<bool> fill_more();
		async::task<bool> read_line(std::string& out);
		async::task<bool> begin_next_chunk();

		std::shared_ptr<io::stream> source_;
		std::string buffer_;
		bool eof_source_ = false;
		bool finished_ = false;
		std::size_t remaining_in_chunk_ = 0;
		bool need_new_chunk_ = true;
	};

}
