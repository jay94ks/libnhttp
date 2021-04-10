#include "http_query_string.hpp"

namespace nhttp {

	int32_t nhttp::http_query_string::try_parse(http_query_string& dst, const std::string& src) {
		const char* src_s = src.c_str();
		const char* src_e = src_s + src.size();

		while (src_s < src_e) {
			const char* src_m = (const char*)memchr(src_s, '&', size_t(src_e - src_s));

			if (!src_m) {
				src_m = src_e;
			}

			const char* kv_sep = (const char*)memchr(src_s, '=', size_t(src_m - src_s));

			if (kv_sep) {
				std::string key(src_s, size_t(kv_sep - src_s));
				std::string val(kv_sep + 1, size_t(src_m - kv_sep - 1));
				dst.kv.emplace(urldecode(key), urldecode(val));
			}

			else {
				std::string key(src_s, size_t(src_m - src_s));
				dst.kv.emplace(urldecode(key), "");
			}

			src_s = src_m + 1;
		}

		return 0;
	}

	bool http_query_string::stringify(std::string& out) const {
		for (const auto& pair : kv) {
			if (out.size()) {
				out.push_back('&');
			}

			out.append(urlencode(pair.first));
			if (pair.second.size()) {
				out.push_back('=');
				out.append(urlencode(pair.second));
			}
		}

		return true;
	}

}