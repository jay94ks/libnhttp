#pragma once

#include <ctime>
#include <string>
#include <string_view>

namespace nhttp::protocol {

	/* formats as RFC 1123 ("HTTP-date"), e.g. "Tue, 15 Nov 1994 08:12:31 GMT". */
	std::string format_http_date(std::time_t time);

	/* parses an RFC 1123 HTTP-date. returns -1 on failure. */
	std::time_t parse_http_date(std::string_view text) noexcept;

}
