#include "http_header.hpp"
#include "../utils/strings.hpp"

namespace nhttp {
	int32_t http_header::try_parse(http_header& dst, const char* src, size_t max, bool by_receiving) {
		/**
		* HEADER: (VALUE[;,]?)*
		* => Minimum: more than 1 characters.
		*
		* expects:
		* 1. header-name: value haha; expiry=15\r\n
		* 2. header-name: value haha; expiry=15
		*/

		if (!src || !max || max <= 1) return 0;
		const char* src_o = src;
		const char* lf = (const char*)memchr(src, '\n', max);
		if (!lf) return 0;

		while (src < lf && *src == ' ' || *src == '\t') { ++src; --max; }
		if ( src >= lf)
			return -1;
		const char* col = (const char*)memchr(src, ':', max);
		if (!col || col == src)
			return 0;

		const char* src_e = col++;
		max -= size_t(col - src);

		while (*src_e == ' ' || *src_e == '\t') { --src_e; }
		while (max && (*col == ' ' || *col == '\t')) { ++col; --max; }

		dst.set_name(std::string(src, size_t(src_e - src)));
		if (lf) {
			const char* lf_b = lf;

			while (col < lf && (*lf == ' ' || *lf == '\t' || *lf == '\r' || *lf == '\n')) { --lf; }
			++lf;

			if (col < lf) {
				dst.set_value(std::string(col, size_t(lf - col)));
			}

			return int32_t(lf_b - src_o + 1);
		}

		size_t len = strnlen(col, max);
		dst.set_value(std::string(col, len));
		return int32_t(col - src_o + len);
	}

	/**
	* stringify header to generate http request.
	* @returns false if name not set.
	*/

	bool http_header::stringify(std::string& out_string, bool with_crlf) const {
		if (!name.size())
			return false;

		if (value.size()) {
			out_string.append(name);
			out_string.append(": ", 2);
			out_string.append(value);

			if (with_crlf)
				out_string.append("\r\n", 2);
		}

		return true;
	}
}