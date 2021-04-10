#pragma once
#include "../types.hpp"
#include "../utils/urlencode.hpp"

namespace nhttp {

	/**
	 * class http_query_string.
	 * parse or stringify URI query string.
	 * this can handle application/www-urlencoded.
	 */
	class NHTTP_API http_query_string {
	public:
		std::map<std::string, std::string> kv;

	public:
		/**
		 * parse query string from http request.
		 * @returns
		 *	1. =  0: success.
		 */
		static int32_t try_parse(http_query_string& dst, const std::string& src);

		bool stringify(std::string& out) const;
		inline std::string stringify() const {
			std::string out;
			stringify(out);
			return out;
		}

	public:
		inline bool has(const std::string& key) const { return kv.find(key) != kv.end(); }
		
		/* @warn: don't store returned pointer! */
		inline const char* get(const std::string& key) const {
			auto elem = kv.find(key);

			if (elem != kv.end())
				return elem->second.c_str();

			return nullptr;
		}

		inline void set(const std::string& key, const char* val, ssize_t len = -1) {
			if (len < 0)
				 kv.emplace(key, std::string(val));
			else kv.emplace(key, std::string(val, len));
		}
	};

}