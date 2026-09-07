#pragma once

#include "http_method.hpp"
#include "http_query_string.hpp"

#include <cstddef>
#include <string>

namespace nhttp::protocol {

	/**
	 * class http_resource.
	 * a parsed HTTP request line: method, decoded path, query string, and
	 * protocol version.
	 */
	class http_resource {
	public:
		http_method method;
		std::string path;       // percent-decoded, e.g. "/a b"
		std::string raw_path;   // as received, still percent-encoded, e.g. "/a%20b"
		http_query_string query;
		int http_major = 1;
		int http_minor = 1;

	public:
		/**
		 * parses "METHOD /path?query HTTP/x.y" up to and including its
		 * terminating '\n'.
		 * @returns >0 = bytes consumed, 0 = need more data, <0 = malformed.
		 */
		static std::ptrdiff_t try_parse(const char* data, std::size_t max, http_resource& out);
	};

}
