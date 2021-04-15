#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"

namespace nhttp {
	/* encode url string. */
	inline std::string urlencode(const std::string& str) {
		const char* src_end = str.c_str() + str.size();
		const char* src = str.c_str();
		std::string retval;

		while (src < src_end) {
			char c = *src++;

			if (c == ' ')
				retval.push_back('+');
				
			else if (
				(c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				 c == '@' || c == '/'  ||  c == '.' || c == '-' ||   c == '_' ||
				 c == '~' || c == ';'  ||  c == '!')
			{
				retval.push_back(c);
			}

			else {
				char temp[3];
				char _1 = (c & 0xf0) >> 4,
				     _2 = (c & 0x0f) >> 0;

				temp[0] = '%';
				temp[1] = _1 >= 10 ? (_1 - 10) + 'a' : _1 + '0';
				temp[2] = _2 >= 10 ? (_2 - 10) + 'a' : _2 + '0';
				
				retval.append(temp, 3);
			}
		}

		return retval;
	}

	/* decode url string. */
	inline std::string urldecode(const std::string& str) {
		const char* src_end = str.c_str() + str.size();
		const char* src = str.c_str();
		std::string retval;

		while (src < src_end) {
			const char* p = (const char*)memchr(src, '%', size_t(src_end - src));

			/* append front strings. */
			if (!p) {
				/* replace '+' to ' '. */
				while (src < src_end) {
					p = (const char*)memchr(src, '+', size_t(src_end - src));

					if (!p) {
						retval.append(src);
						break;
					}

					retval.append(src, size_t(p - src));
					retval.push_back(' ');
					src = p + 1;
				}

				break;
			}
			else if (size_t(p - src) > 0) {
				/* replace '+' to ' '. */
				while (src < p) {
					auto k = (const char*)memchr(src, '+', size_t(p - src));

					if (!k) {
						retval.append(src, size_t(p - src));
						break;
					}

					retval.append(src, size_t(k - src));
					retval.push_back(' ');
					src = k + 1;
				}
			}

			/* if incompleted, waste lefts.*/
			if (p + 2 == src_end) break;

			char _1 = ::tolower(*(++p));
			char _2 = ::tolower(*(++p));
			char _r = 0;

			if (_1 >= '0' && _1 <= '9')      _r |= char((_1 - '0' +  0) << 4);
			else if (_1 >= 'a' && _1 <= 'f') _r |= char((_1 - 'a' + 10) << 4);

			if (_2 >= '0' && _2 <= '9')      _r |= char((_2 - '0' +  0) << 0);
			else if (_2 >= 'a' && _2 <= 'f') _r |= char((_2 - 'a' + 10) << 0);

			retval.push_back(_r);
			src = p + 1;
		}

		return retval;
	}
}