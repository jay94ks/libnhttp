#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"

namespace nhttp {
	/* method flags. */
	enum http_method_flags {
		/* can have request content. */
		NMETHOD_REQUEST_CONTENT = 1,

		/* can have response content. */
		NMETHOD_RESPONSE_CONTENT = 2,
		
		/* can alter server-state. */
		NMETHOD_ALTER_STATE = 4,

		/* can idempotent */
		NMETHOD_IDEMPOTENT = 8,

		/* can be cached or not. */
		NMETHOD_CACHEABLE = 16,

		/* can be cached conditionally. */
		NMETHOD_CONDITIONAL_CACHEABLE = 32
	};

	/**
	 * class method.
	 * represents http request method.
	 */
	class NHTTP_API http_method {
	private:
		std::string name;
		int32_t well_id;
		uint8_t flags;

	public:
		struct invalid_t    { int _1; const char* _2; int _3; };
		struct well_known_t { int _1; const char* _2; int _3; };

		/* Pseudo method and well-known methods: */
		static constexpr invalid_t	  NONE		= {-1, 0,			0 };
		static constexpr well_known_t GET		= { 0, "GET",		NMETHOD_RESPONSE_CONTENT | NMETHOD_IDEMPOTENT		| NMETHOD_CACHEABLE };
		static constexpr well_known_t HEAD		= { 1, "HEAD",		NMETHOD_IDEMPOTENT		 | NMETHOD_CACHEABLE };
		static constexpr well_known_t POST		= { 2, "POST",		NMETHOD_REQUEST_CONTENT  | NMETHOD_RESPONSE_CONTENT | NMETHOD_ALTER_STATE | NMETHOD_CONDITIONAL_CACHEABLE };
		static constexpr well_known_t PUT		= { 3, "PUT" ,		NMETHOD_REQUEST_CONTENT  | NMETHOD_ALTER_STATE		| NMETHOD_IDEMPOTENT };
		static constexpr well_known_t DELETE	= { 4, "DELETE",	NMETHOD_REQUEST_CONTENT  | NMETHOD_RESPONSE_CONTENT | NMETHOD_ALTER_STATE | NMETHOD_IDEMPOTENT };
		static constexpr well_known_t CONNECT	= { 5, "CONNECT",	NMETHOD_RESPONSE_CONTENT | NMETHOD_ALTER_STATE };
		static constexpr well_known_t OPTIONS	= { 6, "OPTIONS",	NMETHOD_RESPONSE_CONTENT | NMETHOD_IDEMPOTENT };
		static constexpr well_known_t TRACE		= { 7, "TRACE",		NMETHOD_RESPONSE_CONTENT | NMETHOD_IDEMPOTENT };
		static constexpr well_known_t PATCH		= { 8, "PATCH",		NMETHOD_REQUEST_CONTENT  | NMETHOD_RESPONSE_CONTENT | NMETHOD_ALTER_STATE };

		/* All well-known methods: */
		static constexpr well_known_t ALL[] = { GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH };
		static constexpr size_t		  ALL_COUNT = sizeof(ALL) / sizeof(well_known_t);

	public:
		http_method(invalid_t) : well_id(-1), flags(0) { }
		http_method(well_known_t w) : name(w._2), well_id(w._1), flags(w._3) { }
		http_method(const char* name) : name(name), well_id(-1), flags(0) { qualify(); }
		http_method(const char* name, size_t len) : name(name, len), well_id(-1), flags(0) { qualify(); }

	public:
		inline operator bool() const { return name.size(); }
		inline bool operator !() const { return !name.size(); }

		inline bool operator ==(const invalid_t&) const { return !name.size(); }
		inline bool operator !=(const invalid_t&) const { return  name.size(); }

		inline bool operator ==(const http_method& m) const { return flags && m.flags ? well_id == m.well_id : name == m.name; }
		inline bool operator !=(const http_method& m) const { return flags && m.flags ? well_id != m.well_id : name != m.name; }

		inline bool operator ==(const well_known_t& w) const { return well_id >= 0 ? well_id == w._1 : name == w._2; }
		inline bool operator !=(const well_known_t& w) const { return well_id >= 0 ? well_id != w._1 : name != w._2; }

		/* for std::map. */
		inline bool operator <=(const http_method& m) const { return flags && m.flags ? well_id <= m.well_id : stricmp(name.c_str(), m.name.c_str()) <= 0; }
		inline bool operator >=(const http_method& m) const { return flags && m.flags ? well_id >= m.well_id : stricmp(name.c_str(), m.name.c_str()) >= 0; }
		inline bool operator < (const http_method& m) const { return flags && m.flags ? well_id <  m.well_id : stricmp(name.c_str(), m.name.c_str()) <  0; }
		inline bool operator > (const http_method& m) const { return flags && m.flags ? well_id >  m.well_id : stricmp(name.c_str(), m.name.c_str()) >  0; }

	public:
		inline bool is(uint8_t flag) const { return (flags & flag) != 0; }
		inline bool is_invalid() const { return !name.size(); }
		inline bool is_well_known() const { return well_id >= 0; }
		inline const char* c_str() const { return name.c_str(); }
		inline size_t c_len() const { return name.size(); }

	private:
		/* trim whitespaces. */
		inline void qualify() {
			std::transform(name.begin(), name.end(), name.begin(), ::toupper); /* to upper and ltrim, rtrim. */
			name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char ch) { return !std::isspace(ch); }));
			name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), name.end());

			for (int32_t i = 0; i < ALL_COUNT; i++) {
				if (!strcmp(ALL[i]._2, name.c_str())) {
					well_id = ALL[i]._1;
					flags = ALL[i]._3;
					break;
				}
			}
		}
	};

}