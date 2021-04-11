#pragma once
#include "../types.hpp"
#include "../depends/utf8.h"
#include <string>

namespace nhttp {
	/**
	 * struct nstring.
	 * string with length (only for compile-time strings).
	 */
	struct nstring {
		const char* string;
		size_t length;

		nstring() : string(0), length(0) { }

		template<size_t size>
		constexpr nstring(const char(&name)[size]) : string(name), length(size - 1) { }
	};

#ifdef _MSC_VER
	inline auto strnicmp(const char* left, const char* right, size_t n) {
		return _strnicmp(left, right, n);
	}

	inline auto stricmp(const char* left, const char* right) {
		return _stricmp(left, right);
	}
#else
	inline auto strnicmp(const char* left, const char* right, size_t n) {
		return strncasecmp(left, right, n);
}

	inline auto stricmp(const char* left, const char* right) {
		return strcasecmp(left, right);
	}
#endif

	inline auto strnicmp(const char* left, const std::string& right) {
		return strnicmp(left, right.c_str(), right.size());
	}

	inline auto strcmp_x(const std::string& left, const std::string& right) {
		if (left.size() <= right.size())
			return strncmp(left.c_str(), right.c_str(), right.size());

		return strncmp(left.c_str(), right.c_str(), left.size());
	}

	inline int64_t to_int64(const char* str, int32_t radix = 10, size_t max = -1) {
		int64_t n = 0;

		while (*str && max--) {
			int64_t c = *str++;
			n *= radix;

			if (c >= '0' && c <= '9')
				n += (c - '0');

			else if (c >= 'a')
				n += (c - 'a') + 10;

			else if (c >= 'A')
				n += (c - 'A') + 10;

			else break;
		}

		return n;
	}

	inline int64_t to_int64(const std::string& s, int32_t radix = 10) {
		return to_int64(s.c_str(), radix, s.size());
	}

	inline void to_hex(std::string& out, uint64_t n, bool no_trim = false) {
		char temp[16]; // 16 bytes.
		ssize_t x = -1;

		for (ssize_t i = 0; i < 8; ++i) {
			uint8_t byte = uint8_t((n >> ((7 - i) * 8)) & 0xff);

			char low4 = ((byte >> 4) & 0x0f), high4 = ((byte >> 0) & 0x0f);

			if (low4 && x < 0) x = i * 2;
			if (high4 && x < 0) x = i * 2 + 1;

			temp[i * 2 + 0] = low4 >= 10 ? ((low4 - 10) + 'a') : low4 + '0';
			temp[i * 2 + 1] = high4 >= 10 ? ((high4 - 10) + 'a') : high4 + '0';
		}

		if (no_trim) {
			out.append(temp, 16);
			return;
		}

		if (x < 0)
			out.push_back('0');

		else out.append(temp + x, size_t(16 - x));
	}

	inline size_t to_hex(char* out, size_t max, uint64_t n, bool no_trim = false) {
		char temp[16]; // 16 bytes.
		ssize_t x = -1;

		for (ssize_t i = 0; i < 8; ++i) {
			uint8_t byte = uint8_t((n >> ((7 - i) * 8)) & 0xff);

			char low4 = ((byte >> 4) & 0x0f), high4 = ((byte >> 0) & 0x0f);

			if (low4 && x < 0) x = i * 2;
			if (high4 && x < 0) x = i * 2 + 1;

			temp[i * 2 + 0] = low4 >= 10 ? ((low4 - 10) + 'a') : low4 + '0';
			temp[i * 2 + 1] = high4 >= 10 ? ((high4 - 10) + 'a') : high4 + '0';
		}

		if (no_trim) {
			memcpy(out, temp, max > 16 ? 16 : max);
			return max > 16 ? 16 : max;
		}

		if (x < 0) {
			if (max) {
				*out = '0';
				return 1;
			}

			return 0;
		}

		memcpy(out, temp + x, max > size_t(16 - x) ? size_t(16 - x) : max);
		return max > size_t(16 - x) ? size_t(16 - x) : max;
	}

	inline void to_hex32(std::string& out, uint32_t n, bool no_trim = false) {
		char temp[8]; // 16 bytes.
		ssize_t x = -1;

		for (ssize_t i = 0; i < 4; ++i) {
			uint8_t byte = uint8_t((n >> ((3 - i) * 8)) & 0xff);

			char low4 = ((byte >> 4) & 0x0f), high4 = ((byte >> 0) & 0x0f);

			if (low4 && x < 0) x = i * 2;
			if (high4 && x < 0) x = i * 2 + 1;

			temp[i * 2 + 0] = low4 >= 10 ? ((low4 - 10) + 'a') : low4 + '0';
			temp[i * 2 + 1] = high4 >= 10 ? ((high4 - 10) + 'a') : high4 + '0';
		}

		if (no_trim) {
			out.append(temp, 8);
			return;
		}

		if (x < 0)
			out.push_back('0');

		else out.append(temp + x, size_t(8 - x));
	}

	namespace _ {
	template<size_t from = sizeof(wchar_t), size_t to = sizeof(char)>
	struct utf8_fn {
		template<typename from_it, typename to_it>
		inline static to_it fn(from_it b, from_it e, to_it it) { return it; }
	};

#define UTF8_FN_IDENTITY(n) \
		template<> struct utf8_fn<n, n> { \
			template<typename from_it, typename to_it> \
			inline static to_it fn(from_it b, from_it e, to_it it) { return std::copy(b, e, it); } \
		};

#define UTF8_FN_X_TO_Y(x, y, _fn) \
		template<> struct utf8_fn<x, y> { \
			template<typename from_it, typename to_it> \
			inline static to_it fn(from_it b, from_it e, to_it it) { return _fn(b, e, it); } \
		};

	/* declare identity conversions. */
	UTF8_FN_IDENTITY(8); UTF8_FN_IDENTITY(16); UTF8_FN_IDENTITY(32);

	/* declare x to y conversions. */
	UTF8_FN_X_TO_Y(16, 8, utf8::unchecked::utf16to8); UTF8_FN_X_TO_Y(32, 8, utf8::unchecked::utf32to8);
	UTF8_FN_X_TO_Y(8, 16, utf8::unchecked::utf8to16); UTF8_FN_X_TO_Y(8, 32, utf8::unchecked::utf8to32);

	template<typename from_it, typename to_it>
	inline to_it cvt_utf_x_to_y(from_it b, from_it e, to_it it) {
		using from_type = typename std::decay<decltype(*std::declval<from_it>())>::type;
		using to_type = typename to_it::container_type::value_type;

		return utf8_fn<sizeof(from_type) * 8, sizeof(to_type) * 8>::fn(std::move(b), std::move(e), std::move(it));
	}
	}

	inline std::string wcs_to_mbs(const std::wstring& in) {
		std::string mbs;

		_::cvt_utf_x_to_y(in.begin(), in.end(), std::back_inserter(mbs));

		return mbs;
	}

	inline std::wstring mbs_to_wcs(const std::string& in) {
		std::wstring wcs;

		_::cvt_utf_x_to_y(in.begin(), in.end(), std::back_inserter(wcs));

		return wcs;
	}

	inline uint32_t hash_djb(const char* str, size_t size) {
		uint32_t hash = 5381;

		while (size--) {
			hash = (((hash << 5) + hash) + *str++);
		}

		return hash;
	}

	inline const char* ltrim(const char* str, size_t max = -1) {
		if (str) {
			while (max && (*str == ' ' || *str == '\t')) {
				++str;
				--max;
			}
		}
		return str;
	}


}