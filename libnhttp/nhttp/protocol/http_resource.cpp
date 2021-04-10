#include "http_resource.hpp"

namespace nhttp {
	int32_t http_resource::try_parse(http_resource& dst, const char* src, size_t max, bool by_receiving) {
		/**
		* METHOD (3) + ' ' * 2 (2) + at-least '/' (1) + HTTP/x.y (6)
		* => Minimum: GET / HTTP/x.y - 12 characters.
		*
		* expects:
		* 1. METHOD /path/to/resource HTTP/x.y\r\n
		* 2. METHOD /path/to/resource HTTP/x.y
		*/
		if (!src || !max || max < 12)  return 0;
		const char* lf = (const char*)memchr(src, '\n', max);
		if (!lf) return 0;

		const char* sp_1 = (const char*)memchr(src, ' ', max);
		const char* sp_2 = sp_1 ? (const char*)memchr(sp_1 + 1, ' ', max - size_t(sp_1 - src - 1)) : nullptr;

		/**
		* METHOD, PATH and PROTOCOL.
		* 1. sp_2 > lf:		against [\n HTTP].
		* 2. lf - 1 == sp_2:	against  [GET / ].\r
		*/
		if (!sp_1 || !sp_2 || sp_1 == src) 
			return -1;

		const char* sp_1_e = sp_2; /* store sp_2 for checking end of sp_1. */
		size_t sp_2_l = max - (size_t(sp_2 - src - 1));
		if (sp_2_l <= 5 || strnicmp(++sp_2, "HTTP/", 5))
			return -2; sp_2 += 5; sp_2_l -= 5;

		if (sp_2_l < 3 || (sp_2[0] != '1' && sp_2[1] != '.' && sp_2[2] != '0' && sp_2[2] != '1'))
			return -2; dst.set_major_ver(1); dst.set_minor_ver(sp_2[2] - '0');

		dst.set_method(http_method(src, size_t(sp_1 - src)));
		dst.set_path(std::string(sp_1 + 1, size_t(sp_1_e - sp_1 - 1)));

		if (lf) {
			return int32_t(lf - src + 1);
		}

		return int32_t(sp_2 - src + 3);
	}

	/**
	* stringify resource header to generate http request.
	* @returns false if method not set.
	*/
	bool http_resource::stringify(std::string& out_string, bool with_crlf) const {
		if (_method) {
			char ver[3];
		
			ver[0] = _ver_major + '0'; ver[1] = '.';
			ver[2] = _ver_minor + '0';

			out_string.append(_method.c_str(), _method.c_len());
			out_string.push_back(' ');

			out_string.append(_full_path);
			out_string.push_back(' ');

			out_string.append("HTTP/", 5);
			out_string.append(ver, 3);

			if (with_crlf)
				out_string.append("\r\n", 2);

			return true;
		}

		return false;
	}
}