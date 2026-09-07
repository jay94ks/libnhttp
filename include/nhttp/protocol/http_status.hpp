#pragma once

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

	private:
		int code_;
	};

}
