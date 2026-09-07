#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace nhttp::protocol {

	/**
	 * class http_query_string.
	 * a parsed "key=value&key2=value2" query string (the part of a URL after '?').
	 */
	class http_query_string {
	public:
		static http_query_string parse(std::string_view raw);

	public:
		const std::string* get(std::string_view key) const noexcept;
		bool isset(std::string_view key) const noexcept { return get(key) != nullptr; }

		std::size_t size() const noexcept { return values_.size(); }
		auto begin() const noexcept { return values_.begin(); }
		auto end() const noexcept { return values_.end(); }

	private:
		std::map<std::string, std::string, std::less<>> values_;
	};

}
