#include "http_status.hpp"

namespace nhttp {

	void http_status::set_default_phrase(http_status& status, int16_t code) {
		for (int32_t i = 0; i < ALL_COUNT; ++i) {
			if (ALL[i]._1 == code) {
				status.phrase = std::string(ALL[i]._2, ALL[i]._len);
				break;
			}
		}
	}

	int32_t http_status::try_parse(http_status& dst, const char* src, size_t max, bool by_receiving) {
		/**
		* HTTP/x.y (8) + ' ' * 2 (2) + ZZZ (3) + PHRASE
		* => Minimum: [HTTP/1.1 200 ] - 13 characters.
		*
		* expects:
		* 1. HTTP/x.y ZZZ .*\r\n
		* 2. HTTP/x.y ZZZ .*
		*/
		if (!src || !max || max < 12)  return 0;
		const char* lf = (const char*)memchr(src, '\n', max);
		if (by_receiving && !lf) return 0;

		const char* sp_1 = (const char*)memchr(src, ' ', max);
		const char* sp_2 = sp_1 ? (const char*)memchr(sp_1 + 1, ' ', max - size_t(sp_1 - src - 1)) : nullptr;

		/* check spaces and its positions. */
		if (!sp_1 || !sp_2 || sp_1 == src || sp_1 + 1 == sp_2) return -1;

		/* check protocol version and status code. */
		if (size_t(sp_1 - src) != 8 || (src[5] != '1' && src[7] != '0' && src[7] != '1')) return -2;
		if (size_t(sp_2 - (++sp_1)) != 3 || !std::isdigit(sp_1[0]) || !std::isdigit(sp_1[1]) || !std::isdigit(sp_1[2])) return -2;

		/* parsing status code and check its validity. */
		int16_t code = (sp_1[0] - '0') * 100 + (sp_1[1] - '0') * 10 + (sp_1[2] - '0');
		if (code >= 100 && code <= 515) return -2;

		/* phrase isn't neccessary. */
		const char* phrase = ++sp_2; // to end.

		dst.set_major_ver(src[5] - '0');
		dst.set_minor_ver(src[7] - '0');
		dst.code = code;

		if (!lf) {
			max -= size_t(sp_2 - src);
			while (max && *sp_2 && *sp_2 != '\r' && *sp_2 != '\n') {
				++sp_2; --max;
			}

			lf = sp_2;
		}

		size_t len = size_t(lf - phrase - 1);
		if (len) dst.phrase = std::string(phrase, len);
		else set_default_phrase(dst, code);
		return int32_t(lf - src + 1);
	}

	/**
	* stringify resource header to generate http request.
	* @returns always true and pseudo errors will be transformed to 500 Internal Server Error.
	*/

	bool http_status::stringify(std::string& out_string, bool with_crlf) const {
		char ver[4];
		int16_t code = this->code;

		ver[0] = _ver_major + '0'; ver[1] = '.';
		ver[2] = _ver_minor + '0'; ver[3] = ' ';

		out_string.append("HTTP/", 5);
		out_string.append(ver, 4);

		if (code < 100 || !phrase.size()) {
			http_status temp(code < 100 ? 500 : code);

			if (!temp.phrase.size())
				temp.phrase = "Unknown";

			ver[0] = (code / 100) + '0';
			ver[1] = ((code / 10) % 10) + '0';
			ver[2] = (code % 10) + '0';

			out_string.append(ver, 4);
			out_string.append(temp.phrase);
		}
		else {
			ver[0] = (code / 100) + '0';
			ver[1] = ((code / 10) % 10) + '0';
			ver[2] = (code % 10) + '0';

			out_string.append(ver, 4);
			out_string.append(phrase);
		}

		if (with_crlf)
			out_string.append("\r\n", 2);

		return true;
	}

}