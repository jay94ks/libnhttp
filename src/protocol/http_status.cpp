#include "nhttp/protocol/http_status.hpp"

#include <cstring>

namespace nhttp::protocol {

	std::string_view http_status::reason_phrase() const noexcept {
		switch (code_) {
		case 100: return "Continue";
		case 101: return "Switching Protocols";
		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 204: return "No Content";
		case 206: return "Partial Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 406: return "Not Acceptable";
		case 408: return "Request Timeout";
		case 409: return "Conflict";
		case 410: return "Gone";
		case 411: return "Length Required";
		case 412: return "Precondition Failed";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 415: return "Unsupported Media Type";
		case 416: return "Range Not Satisfiable";
		case 417: return "Expectation Failed";
		case 426: return "Upgrade Required";
		case 429: return "Too Many Requests";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
		default: return "Unknown";
		}
	}

	void http_status::write_status_line(std::string& out, int http_minor_version) const {
		out += "HTTP/1.";
		out += std::to_string(http_minor_version);
		out += ' ';
		out += std::to_string(code_);
		out += ' ';
		out += reason_phrase();
		out += "\r\n";
	}

	std::ptrdiff_t http_status::try_parse(const char* data, std::size_t max, http_status& out, int& http_major, int& http_minor) {
		const void* nl_ptr = std::memchr(data, '\n', max);

		if (!nl_ptr)
			return 0;

		const char* nl = static_cast<const char*>(nl_ptr);
		std::size_t line_len = static_cast<std::size_t>(nl - data);
		const std::size_t consumed = line_len + 1;

		if (line_len > 0 && data[line_len - 1] == '\r')
			--line_len;

		const std::string_view line(data, line_len);

		// "HTTP/x.y CODE Reason..." — reason phrase may be empty or absent.
		if (line.size() < 12 || line.substr(0, 5) != "HTTP/" || line[6] != '.' || line[8] != ' ')
			return -1;

		const char major_ch = line[5];
		const char minor_ch = line[7];

		if (major_ch < '0' || major_ch > '9' || minor_ch < '0' || minor_ch > '9')
			return -1;

		const std::size_t code_start = 9;
		const std::size_t sp2 = line.find(' ', code_start);
		const std::string_view code_part = line.substr(code_start, sp2 == std::string_view::npos ? std::string_view::npos : sp2 - code_start);

		if (code_part.size() != 3)
			return -1;

		int code = 0;

		for (const char c : code_part) {
			if (c < '0' || c > '9')
				return -1;

			code = code * 10 + (c - '0');
		}

		http_major = major_ch - '0';
		http_minor = minor_ch - '0';
		out = http_status(code);

		return static_cast<std::ptrdiff_t>(consumed);
	}

}
