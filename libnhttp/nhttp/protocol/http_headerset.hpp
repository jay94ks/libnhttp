#pragma once
#include "http_header.hpp"
#include "../utils/strings.hpp"

namespace nhttp {
	/**
	 * class headers.
	 * container for headers.
	 */
	class NHTTP_API http_headers {
	public:
		using iterator = typename std::vector<http_header>::iterator;
		using const_iterator = typename std::vector<http_header>::const_iterator;

	public:
		std::vector<http_header> vec;

		http_headers() { }
		http_headers(const http_headers& h) : vec(h.vec) { }
		http_headers(http_headers&& h) : vec(std::move(h.vec)) { }

		inline http_headers& operator =(const http_headers& o) { vec = o.vec; return *this; }
		inline http_headers& operator =(http_headers&& o) { vec = std::move(o.vec); return *this; }

	public:
		/**
		 * stringify headers to generate http request.
		 * @returns always true.
		 */
		bool stringify(std::string& out_string, bool with_crlf = true);

	public:
		/* determines header set or not. */
		inline bool isset(const std::string& name) const { return find_one(name) != vec.end(); }
		inline bool isset(const http_header::well_known_t& name) const { return find_one(name) != vec.end(); }

		/* get header. @warn don't store returned pointer! */
		inline const char* get(const std::string& name) const {
			auto item = find_one(name);

			if (item != vec.end())
				return item->get_value().c_str();

			return nullptr;
		}

		/* get header. @warn don't store returned pointer! */
		inline const char* get(const http_header::well_known_t& name) const {
			auto item = find_one(name);

			if (item != vec.end())
				return item->get_value().c_str();

			return nullptr;
		}

		/* set header. */
		inline http_headers& set(const std::string& name, const std::string& value, bool replace = true) {
			if (replace)
				unset(name);

			vec.push_back(http_header(name, value));
			return *this;
		}

		/* set header. */
		inline http_headers& set(const http_header::well_known_t& name, const std::string& value, bool replace = true) {
			http_header _header(name);
			_header.set_value(value);

			if (replace)
				unset(name);

			vec.push_back(std::move(_header));
			return *this;
		}

		/* find header by its name. */
		inline const_iterator find_one(const std::string& name) const { return find_one(vec.cbegin(), name); }
		inline const_iterator find_one(const_iterator from, const std::string& name) const {
			if (from != vec.cend()) {
				for (auto i = from; i != vec.cend(); ++i) {
					const auto& each = i->get_name();

					if (each.size() == name.size() && !strnicmp(each.c_str(), name.c_str(), each.size())) {
						return i;
					}

				}
			}
			return vec.cend();
		}

		/* find header by its name. */
		inline const_iterator find_one(const http_header::well_known_t& name) const { return find_one(vec.cbegin(), name); }
		inline const_iterator find_one(const_iterator from, const http_header::well_known_t& name) const {
			if (from != vec.cend()) {
				for (auto i = from; i != vec.cend(); ++i) {
					if (*i == name) return i;
				}
			}
			return vec.cend();
		}

		/* unset header by its name. */
		inline bool unset(const std::string& name, bool all = true) {
			int32_t n = 0;

			for (ssize_t i = ssize_t(vec.size()) - 1; i >= 0; --i) {
				auto each = vec[i].get_name();

				if (each.size() == name.size() && 
					!strnicmp(each.c_str(), name.c_str(), each.size())) 
				{
					vec.erase(vec.begin() + i); ++n;

					if (!all)
						break;
				}
			}

			return n > 0;
		}

		/* unset header by its name. */
		inline bool unset(const http_header::well_known_t& name, bool all = true) {
			int32_t n = 0;

			for (ssize_t i = ssize_t(vec.size()) - 1; i >= 0; --i) {
				if (vec[i] == name) {
					vec.erase(vec.begin() + i); ++n;

					if (!all)
						break;
				}
			}

			return n > 0;
		}

	};
}