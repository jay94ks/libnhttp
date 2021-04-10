#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"

namespace nhttp {
namespace _ {
	struct well_known_http_status_t {
		int16_t _1;
		size_t _len;
		const char* _2;

		template<size_t size>
		constexpr well_known_http_status_t(int16_t code, const char(&phrase)[size])
			: _1(code), _len(size - 1), _2(phrase) { }
	};
}

	class NHTTP_API http_status {
	public:
		struct pseudo_t { int16_t _; };
		using well_known_t = _::well_known_http_status_t;

		/* 0 ~ 99: pseudo error code. (not really sent) */
		static constexpr pseudo_t _NET_ERROR = { 0 };
		static constexpr pseudo_t _NO_MEMORY = { 1 };
		static constexpr pseudo_t _IO_DENIED = { 2 };

		/* 100 ~ 103: protocol negotiation. */
		static constexpr well_known_t _100 = well_known_t(100, "Continue");
		static constexpr well_known_t _101 = well_known_t(101, "Switching Protocol");
		static constexpr well_known_t _102 = well_known_t(102, "Processing");
		static constexpr well_known_t _103 = well_known_t(103, "Early Hints");

		/* 200 ~ 226: content response or negotiation. */
		static constexpr well_known_t _200 = well_known_t(200, "OK");
		static constexpr well_known_t _201 = well_known_t(201, "Created");
		static constexpr well_known_t _202 = well_known_t(202, "Accepted");
		static constexpr well_known_t _203 = well_known_t(203, "Non-Authoritative Information");
		static constexpr well_known_t _204 = well_known_t(204, "No Content");
		static constexpr well_known_t _205 = well_known_t(205, "Reset Content");
		static constexpr well_known_t _206 = well_known_t(206, "Partial Content");
		static constexpr well_known_t _207 = well_known_t(207, "Multi-Status");
		static constexpr well_known_t _208 = well_known_t(208, "Multi-Status");
		static constexpr well_known_t _226 = well_known_t(226, "IM Used");

		/* 300 ~ 308: resource redirection. */
		static constexpr well_known_t _300 = well_known_t(300, "Multiple Choice");
		static constexpr well_known_t _301 = well_known_t(301, "Moved Permanently");
		static constexpr well_known_t _302 = well_known_t(302, "Found");
		static constexpr well_known_t _303 = well_known_t(303, "See Other");
		static constexpr well_known_t _304 = well_known_t(304, "Not Modified");
		static constexpr well_known_t _307 = well_known_t(307, "Temporary Redirect");
		static constexpr well_known_t _308 = well_known_t(308, "Permanent Redirect");

		/* 400 ~ 451: error codes. */
		static constexpr well_known_t _400 = well_known_t(400, "Bad Request");
		static constexpr well_known_t _401 = well_known_t(401, "Unauthorized");
		static constexpr well_known_t _403 = well_known_t(403, "Forbidden");
		static constexpr well_known_t _404 = well_known_t(404, "Not Found");
		static constexpr well_known_t _405 = well_known_t(405, "Method Not Allowed");
		static constexpr well_known_t _406 = well_known_t(406, "Not Acceptable");
		static constexpr well_known_t _407 = well_known_t(407, "Proxy Authentication Required");
		static constexpr well_known_t _408 = well_known_t(408, "Request Timeout");
		static constexpr well_known_t _409 = well_known_t(409, "Conflict");
		static constexpr well_known_t _410 = well_known_t(410, "Gone");
		static constexpr well_known_t _411 = well_known_t(411, "Length Required");
		static constexpr well_known_t _412 = well_known_t(412, "Precondition Failed");
		static constexpr well_known_t _413 = well_known_t(413, "Payload Too Large");
		static constexpr well_known_t _414 = well_known_t(414, "URI Too Long");
		static constexpr well_known_t _415 = well_known_t(415, "Unsupported Media Type");
		static constexpr well_known_t _416 = well_known_t(416, "Requested Range Not Statisfiable");
		static constexpr well_known_t _417 = well_known_t(417, "Expectation Failed");
		static constexpr well_known_t _421 = well_known_t(421, "Misdirected Request");
		static constexpr well_known_t _422 = well_known_t(422, "Unprocessable Entity");
		static constexpr well_known_t _423 = well_known_t(423, "Locked");
		static constexpr well_known_t _424 = well_known_t(424, "Failed Dependency");
		static constexpr well_known_t _426 = well_known_t(426, "Upgrade Required");
		static constexpr well_known_t _428 = well_known_t(428, "Precondition Required");
		static constexpr well_known_t _429 = well_known_t(429, "Too Many Requests");
		static constexpr well_known_t _431 = well_known_t(431, "Request Header Fields Too Large");
		static constexpr well_known_t _451 = well_known_t(451, "Unavailable For Legal Reasons");

		/* 500 ~ 511: protocol error or can't be handled. */
		static constexpr well_known_t _500 = well_known_t(500, "Internal Server Error");
		static constexpr well_known_t _501 = well_known_t(501, "Not Implemented");
		static constexpr well_known_t _502 = well_known_t(502, "Bad Gateway");
		static constexpr well_known_t _503 = well_known_t(503, "Service Unavailable");
		static constexpr well_known_t _504 = well_known_t(504, "Gateway Timeout");
		static constexpr well_known_t _505 = well_known_t(505, "HTTP Version Not Supported");
		static constexpr well_known_t _511 = well_known_t(511, "Network Authentication Required");

		/* ALL: All predefined response phrases. */
		static constexpr well_known_t ALL[] = { 
			_100, _101, _102, _103, _200, _201, _202, _203, _204, _205, _206, _207, _208, _226,
			_300, _301, _302, _303, _304, _307, _308, _400, _401, _403, _404, _405, _406, _407,
			_408, _409, _410, _411, _412, _413, _414, _415, _416, _417, _421, _422, _423, _424,
			_426, _428, _429, _431, _451, _500, _501, _502, _503, _504, _505, _511
		};

		static constexpr size_t ALL_COUNT = sizeof(ALL) / sizeof(well_known_t);

	private:
		int16_t code;
		std::string phrase;
		int8_t _ver_major;
		int8_t _ver_minor;

	private:
		static void set_default_phrase(http_status& status, int16_t code);

	public:
		http_status() : code(200), _ver_major(1), _ver_minor(1) { set_default_phrase(*this, 200); }
		http_status(const pseudo_t& p) : code(p._), _ver_major(1), _ver_minor(1) { }
		http_status(const well_known_t& w) : code(w._1), phrase(w._2, w._len), _ver_major(1), _ver_minor(1) { }
		http_status(int16_t code, const char* phrase = nullptr)
			: code(code), _ver_major(1), _ver_minor(1)
		{
			if (!phrase)
				set_default_phrase(*this, code);

			else this->phrase = phrase;
		}

	public:
		/**
		 * parse status header from http response.
		 * @param by_receiving for parsing status header in receiving loop.
		 * @returns
		 *	1. >  0: success.
		 *  2. =  0: incompleted.
		 *  3. = -1: invalid string.
		 *  4. = -2: unknown protocol.
		 */
		static int32_t try_parse(http_status& dst, const char* src, size_t max, bool by_receiving = false);

		/**
		 * stringify resource header to generate http request.
		 * @returns always true and pseudo errors will be transformed to 500 Internal Server Error.
		 */
		bool stringify(std::string& out_string, bool with_crlf = true) const;

	public:
		inline bool operator ==(const http_status& o) const { return code == o.code; }
		inline bool operator !=(const http_status& o) const { return code != o.code; }

		inline bool operator ==(const pseudo_t& p) const { return code == p._; }
		inline bool operator !=(const pseudo_t& p) const { return code != p._; }

		inline bool operator ==(const well_known_t& o) const { return code == o._1; }
		inline bool operator !=(const well_known_t& o) const { return code != o._1; }

	public:
		inline int16_t get_code() const { return code; }
		inline const std::string& get_phrase() const { return phrase; }

		inline int8_t get_major_ver() const { return _ver_major; }
		inline int8_t get_minor_ver() const { return _ver_minor; }

		inline void set_major_ver(int8_t v) { _ver_major = v; }
		inline void set_minor_ver(int8_t v) { _ver_minor = v; }

		inline bool is_succeed() const { return code >= 200 && code < 300; }
		inline bool is_failed() const { return !is_succeed(); }

		/* set status code and its phrase. */
		inline void set(int32_t code, const char* phrase = nullptr) {
			if (!phrase)
				set_default_phrase(*this, this->code = code);

			else {
				this->code = code;
				this->phrase = phrase;
			}
		}
	};

}