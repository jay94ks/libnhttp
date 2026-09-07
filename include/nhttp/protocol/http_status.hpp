#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace nhttp::protocol {

	class http_status {
	public:
		http_status() noexcept : code_(200) { }
		explicit http_status(int code) noexcept : code_(code) { }

	public:
		int code() const noexcept { return code_; }

		/* looked up from a table of standard phrases; "Unknown" for anything else. */
		std::string_view reason_phrase() const noexcept;

		bool operator==(const http_status& other) const noexcept { return code_ == other.code_; }
		bool operator!=(const http_status& other) const noexcept { return code_ != other.code_; }

	public:
		/* formats "HTTP/1.x CODE Reason Phrase\r\n" into out. */
		void write_status_line(std::string& out, int http_minor_version = 1) const;

		/**
		 * parses "HTTP/x.y CODE Reason..." up to and including its terminating
		 * '\n' (mirrors http_resource::try_parse's request-line contract, the
		 * client-role counterpart to it — used when reading a response status
		 * line, e.g. from a reverse-proxied upstream).
		 * @returns >0 = bytes consumed, 0 = need more data, <0 = malformed.
		 */
		static std::ptrdiff_t try_parse(const char* data, std::size_t max, http_status& out, int& http_major, int& http_minor);

	private:
		int code_;
	};

}
