#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"
#include "../utils/urlencode.hpp"
#include "http_method.hpp"

namespace nhttp {
	/**
	 * class resource.
	 * specifies the target resource on server.
	 */
	class NHTTP_API http_resource {
	private:
		http_method _method;

		std::string _path;
		std::string _query_string;
		std::string _full_path;

		int32_t _ver_major;
		int32_t _ver_minor;

	public:
		http_resource() : _method(http_method::NONE), _path("/"), _ver_major(1), _ver_minor(1) { }
		http_resource(http_method _method, const std::string& path)
			: _method(_method), _ver_major(1), _ver_minor(1) { set_path(path); }

	public:
		/**
		 * parse resource header from http request.
		 * @param by_receiving for parsing resource header in receiving loop.
		 * @returns
		 *	1. >  0: success.
		 *  2. =  0: incompleted.
		 *  3. = -1: invalid string.
		 *  4. = -2: unknown protocol.
		 */
		static int32_t try_parse(http_resource& dst, const char* src, size_t max, bool by_receiving = false);

		/**
		 * stringify resource header to generate http request.
		 * @returns false if method not set.
		 */
		bool stringify(std::string& out_string, bool with_crlf = true) const;

	public:
		/* get protocol version. */
		inline int32_t get_major_ver() const { return _ver_major; }
		inline int32_t get_minor_ver() const { return _ver_minor; }

		/* get method and path, query string. */
		inline const http_method& get_method() const { return _method; }
		inline const std::string& get_path() const { return _path; }
		inline const std::string& get_query_string() const { return _query_string; }

		/* get encoded path string. */
		inline std::string get_encoded_path() const { return urlencode(_path); }
		inline const std::string& get_encoded_full_path() const { return _full_path; }

	public:
		/* set protocol version. */
		inline void set_major_ver(int32_t v) { _ver_major = v; }
		inline void set_minor_ver(int32_t v) { _ver_minor = v; }

		/* set method and path string. */
		inline void set_method(const http_method& _method) { this->_method = _method; }
		inline void set_path(const std::string& path) {
			size_t s = path.find_first_of('?');
			_full_path = path;

			if (s != std::string::npos) {
				_query_string = path.substr(s + 1);
				_path = urldecode(path.substr(0, s));
			}

			else _path = urldecode(path);
		}

		/* set query string. */
		inline void set_query_string(const std::string& query_string) {
			if ((_query_string = query_string).size()) {
				_query_string.erase(_query_string.begin(),
					std::find_if(_query_string.begin(), _query_string.end(),
						[](unsigned char ch) { return ch != '?'; }));

				(_full_path = _path).push_back('?');
				_full_path.append(_query_string);
			}
		}

	};
}