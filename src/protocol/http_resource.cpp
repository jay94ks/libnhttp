#include "nhttp/protocol/http_resource.hpp"
#include "nhttp/protocol/urlencode.hpp"

#include <cstring>

namespace nhttp::protocol {

	std::ptrdiff_t http_resource::try_parse(const char* data, std::size_t max, http_resource& out) {
		const void* nl_ptr = std::memchr(data, '\n', max);

		if (!nl_ptr)
			return 0;

		const char* nl = static_cast<const char*>(nl_ptr);
		std::size_t line_len = static_cast<std::size_t>(nl - data);
		const std::size_t consumed = line_len + 1;

		if (line_len > 0 && data[line_len - 1] == '\r')
			--line_len;

		const std::string_view line(data, line_len);

		const std::size_t sp1 = line.find(' ');

		if (sp1 == std::string_view::npos)
			return -1;

		const std::size_t sp2 = line.find(' ', sp1 + 1);

		if (sp2 == std::string_view::npos)
			return -1;

		const std::string_view method_part = line.substr(0, sp1);
		const std::string_view target_part = line.substr(sp1 + 1, sp2 - sp1 - 1);
		const std::string_view version_part = line.substr(sp2 + 1);

		// "HTTP/1.1"
		if (version_part.size() != 8 || version_part.substr(0, 5) != "HTTP/" || version_part[6] != '.')
			return -1;

		const char major_ch = version_part[5];
		const char minor_ch = version_part[7];

		if (major_ch < '0' || major_ch > '9' || minor_ch < '0' || minor_ch > '9')
			return -1;

		out.method = http_method(std::string(method_part));
		out.http_major = major_ch - '0';
		out.http_minor = minor_ch - '0';

		const std::size_t qpos = target_part.find('?');

		if (qpos == std::string_view::npos) {
			out.raw_path = std::string(target_part);
			out.query = http_query_string::parse(std::string_view());
		}
		else {
			out.raw_path = std::string(target_part.substr(0, qpos));
			out.query = http_query_string::parse(target_part.substr(qpos + 1));
		}

		out.path = url_decode(out.raw_path);

		return static_cast<std::ptrdiff_t>(consumed);
	}

}
