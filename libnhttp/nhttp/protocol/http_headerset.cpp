#include "http_headerset.hpp"

namespace nhttp {
	bool http_headers::stringify(std::string& out_string, bool with_crlf) {
		if (vec.size()) {
			std::sort(vec.begin(), vec.end(), 
				[](const http_header& l, const http_header& r) {
					return l.get_name().compare(r.get_name()) < 0;
				});

			for (const http_header& each : vec) {
				each.stringify(out_string, true);
			}
		}

		if (with_crlf)
			out_string.append("\r\n", 2);

		return true;
	}
}