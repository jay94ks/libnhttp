#include "nhttp/protocol/http_query_string.hpp"
#include "nhttp/protocol/urlencode.hpp"

namespace nhttp::protocol {

	http_query_string http_query_string::parse(std::string_view raw) {
		http_query_string out;

		std::size_t pos = 0;

		while (pos < raw.size()) {
			std::size_t amp = raw.find('&', pos);

			if (amp == std::string_view::npos)
				amp = raw.size();

			const std::string_view pair = raw.substr(pos, amp - pos);
			pos = amp + 1;

			if (pair.empty())
				continue;

			const std::size_t eq = pair.find('=');
			std::string key;
			std::string value;

			if (eq == std::string_view::npos) {
				key = url_decode(pair);
			}
			else {
				key = url_decode(pair.substr(0, eq));
				value = url_decode(pair.substr(eq + 1));
			}

			out.values_[std::move(key)] = std::move(value);
		}

		return out;
	}

	const std::string* http_query_string::get(std::string_view key) const noexcept {
		const auto it = values_.find(key);
		return it == values_.end() ? nullptr : &it->second;
	}

}
