#include "nhttp/protocol/http_header.hpp"

#include <algorithm>
#include <cstring>
#include <strings.h>

namespace nhttp::protocol {

	bool header_name_equals(std::string_view a, std::string_view b) noexcept {
		if (a.size() != b.size())
			return false;

		return ::strncasecmp(a.data(), b.data(), a.size()) == 0;
	}

	std::ptrdiff_t http_header::try_parse(const char* data, std::size_t max, http_header& out) {
		const void* nl_ptr = std::memchr(data, '\n', max);

		if (!nl_ptr)
			return 0;

		const char* nl = static_cast<const char*>(nl_ptr);
		std::size_t line_len = static_cast<std::size_t>(nl - data);
		const std::size_t consumed = line_len + 1;

		if (line_len > 0 && data[line_len - 1] == '\r')
			--line_len;

		const void* colon_ptr = std::memchr(data, ':', line_len);

		if (!colon_ptr)
			return -1;

		const char* colon = static_cast<const char*>(colon_ptr);
		const std::size_t name_len = static_cast<std::size_t>(colon - data);

		if (name_len == 0)
			return -1;

		const char* value_begin = colon + 1;
		std::size_t value_len = line_len - name_len - 1;

		while (value_len > 0 && (*value_begin == ' ' || *value_begin == '\t')) {
			++value_begin;
			--value_len;
		}

		while (value_len > 0 && (value_begin[value_len - 1] == ' ' || value_begin[value_len - 1] == '\t'))
			--value_len;

		out.name.assign(data, name_len);
		out.value.assign(value_begin, value_len);

		return static_cast<std::ptrdiff_t>(consumed);
	}

	void http_headers::set(std::string name, std::string value) {
		for (http_header& h : headers_) {
			if (header_name_equals(h.name, name)) {
				h.value = std::move(value);
				return;
			}
		}

		headers_.push_back(http_header{ std::move(name), std::move(value) });
	}

	void http_headers::add(std::string name, std::string value) {
		headers_.push_back(http_header{ std::move(name), std::move(value) });
	}

	void http_headers::unset(std::string_view name) {
		headers_.erase(
			std::remove_if(headers_.begin(), headers_.end(),
				[name](const http_header& h) { return header_name_equals(h.name, name); }),
			headers_.end());
	}

	bool http_headers::isset(std::string_view name) const noexcept {
		for (const http_header& h : headers_) {
			if (header_name_equals(h.name, name))
				return true;
		}

		return false;
	}

	const std::string* http_headers::get(std::string_view name) const noexcept {
		for (const http_header& h : headers_) {
			if (header_name_equals(h.name, name))
				return &h.value;
		}

		return nullptr;
	}

	std::vector<std::string> http_headers::get_all(std::string_view name) const {
		std::vector<std::string> out;

		for (const http_header& h : headers_) {
			if (header_name_equals(h.name, name))
				out.push_back(h.value);
		}

		return out;
	}

	void http_headers::write_to(std::string& out) const {
		for (const http_header& h : headers_) {
			out += h.name;
			out += ": ";
			out += h.value;
			out += "\r\n";
		}
	}

}
