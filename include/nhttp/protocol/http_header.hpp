#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nhttp::protocol {

	bool header_name_equals(std::string_view a, std::string_view b) noexcept;

	/* case-insensitive substring search — good enough for the "close"/
	 * "keep-alive"/"chunked"/"upgrade" tokens this library actually checks
	 * for (not full RFC 7230 comma-separated token-list parsing; see
	 * CLAUDE.md's Phase-4 known-simplifications note). */
	bool header_value_contains_token(std::string_view value, std::string_view token) noexcept;

	/* well-known header names, as string_view constants (RFC 7230 field names are ASCII). */
	namespace header_names {
		inline constexpr std::string_view CONTENT_LENGTH = "Content-Length";
		inline constexpr std::string_view CONTENT_TYPE = "Content-Type";
		inline constexpr std::string_view CONTENT_RANGE = "Content-Range";
		inline constexpr std::string_view CONTENT_DISPOSITION = "Content-Disposition";
		inline constexpr std::string_view TRANSFER_ENCODING = "Transfer-Encoding";
		inline constexpr std::string_view HOST = "Host";
		inline constexpr std::string_view CONNECTION = "Connection";
		inline constexpr std::string_view UPGRADE = "Upgrade";
		inline constexpr std::string_view SEC_WEBSOCKET_KEY = "Sec-WebSocket-Key";
		inline constexpr std::string_view SEC_WEBSOCKET_ACCEPT = "Sec-WebSocket-Accept";
		inline constexpr std::string_view SEC_WEBSOCKET_VERSION = "Sec-WebSocket-Version";
		inline constexpr std::string_view ETAG = "ETag";
		inline constexpr std::string_view IF_MATCH = "If-Match";
		inline constexpr std::string_view IF_NONE_MATCH = "If-None-Match";
		inline constexpr std::string_view IF_MODIFIED_SINCE = "If-Modified-Since";
		inline constexpr std::string_view IF_RANGE = "If-Range";
		inline constexpr std::string_view LAST_MODIFIED = "Last-Modified";
		inline constexpr std::string_view RANGE = "Range";
		inline constexpr std::string_view ACCEPT_RANGES = "Accept-Ranges";
		inline constexpr std::string_view CACHE_CONTROL = "Cache-Control";
		inline constexpr std::string_view AUTHORIZATION = "Authorization";
		inline constexpr std::string_view SERVER = "Server";
		inline constexpr std::string_view DATE = "Date";
		inline constexpr std::string_view LOCATION = "Location";
	}

	struct http_header {
		std::string name;
		std::string value;

		/**
		 * parses one "Name: value" line starting at [data, data + max), including
		 * its terminating '\n' (a preceding '\r' is tolerated and stripped).
		 * @returns >0 = bytes consumed, 0 = need more data (no '\n' yet), <0 = malformed.
		 */
		static std::ptrdiff_t try_parse(const char* data, std::size_t max, http_header& out);
	};

	/**
	 * class http_headers.
	 * an ordered collection of headers (order matters for serialization and for
	 * repeatable headers like Set-Cookie). name lookups are case-insensitive.
	 */
	class http_headers {
	public:
		void set(std::string name, std::string value);
		void add(std::string name, std::string value);
		void unset(std::string_view name);

		bool isset(std::string_view name) const noexcept;
		const std::string* get(std::string_view name) const noexcept;
		std::vector<std::string> get_all(std::string_view name) const;

		void write_to(std::string& out) const;

		std::size_t size() const noexcept { return headers_.size(); }
		auto begin() const noexcept { return headers_.begin(); }
		auto end() const noexcept { return headers_.end(); }

	private:
		std::vector<http_header> headers_;
	};

}
