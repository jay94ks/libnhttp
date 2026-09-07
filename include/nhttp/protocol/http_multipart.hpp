#pragma once

#include "http_header.hpp"
#include "../io/stream.hpp"

#include <memory>
#include <optional>
#include <string>

namespace nhttp::protocol {

	class multipart_reader;

	/**
	 * one part of a multipart/form-data body: its headers, plus a stream over
	 * just its bytes. the body stream is only valid while the owning
	 * multipart_reader is alive, and only until the next call to next_part()
	 * (which discards any unread remainder of the current part automatically).
	 */
	struct multipart_part {
		http_headers headers;
		std::shared_ptr<io::stream> body;

		/* the "name" parameter of this part's Content-Disposition header, if any. */
		std::optional<std::string> name() const;

		/* the "filename" parameter of this part's Content-Disposition header, if any
		 * — present when this part is a file upload rather than a plain form field. */
		std::optional<std::string> filename() const;
	};

	/**
	 * class multipart_reader.
	 * reads a multipart/form-data body from an underlying stream, boundary-
	 * delimited, yielding one part at a time without buffering more than a small
	 * lookahead window regardless of how large a part's body is — see
	 * CONCEPTS.md's "streaming first" principle. drive it sequentially: get a
	 * part, read (or ignore) its body, then call next_part() again.
	 */
	class multipart_reader {
	public:
		multipart_reader(std::shared_ptr<io::stream> source, std::string boundary);

	public:
		/* nullopt once the closing boundary has been seen (or the body is malformed). */
		async::task<std::optional<multipart_part>> next_part();

	private:
		friend class multipart_part_stream;

		async::task<bool> fill_more();
		async::task<bool> ensure_delimiter_or_eof();
		async::task<bool> read_header_line(std::string& out);
		async::task<std::size_t> read_part_body(void* buf, std::size_t n);

		std::shared_ptr<io::stream> source_;
		std::string boundary_marker_; // "\r\n--" + boundary
		std::string buffer_;
		bool eof_ = false;
		bool finished_ = false;
		bool part_active_ = false;
		std::size_t delimiter_pos_ = std::string::npos;
	};

}
