#include "http_form_data.hpp"

namespace nhttp {

	//int32_t http_form_data::try_parse_urlencoded(http_form_data& out, const std::shared_ptr<http_content>& content) {
	//	http_query_string query_string;
	//	std::string as_string;
	//	char temp[256];

	//	while (true) {
	//		ssize_t len = content->read(temp, sizeof(temp));

	//		if (len < 0)
	//			break;

	//		if (len >= 0)
	//			as_string.append(temp, len);
	//	}

	//	http_query_string::try_parse(query_string, as_string);
	//	for (auto& each : query_string.kv) {
	//		out.kv.emplace(each.first, nvalue<nval::string>(each.second));
	//	}

	//	return 0;
	//}

}