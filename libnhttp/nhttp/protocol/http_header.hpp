#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"
#include "../utils/urlencode.hpp"

namespace nhttp {
namespace _ {
	struct well_known_header_t {
		const char* _1;
		size_t _len;

		template<size_t size>
		constexpr well_known_header_t(const char(&name)[size]) : _1(name), _len(size - 1) { }
	};
}
	/**
	 * class header.
	 * represents http header.
	 * @returns
	 *	1. =  0: success.
	 *  2. =  1: incompleted.
	 *  3. = -1: invalid string.
	 */
	class NHTTP_API http_header {
	public:
		using well_known_t = _::well_known_header_t;

		/* Well-Known headers: for using by implementation. */
		static constexpr well_known_t TE					= well_known_t( "TE" );
		static constexpr well_known_t AGE					= well_known_t( "Age" );
		static constexpr well_known_t P3P					= well_known_t( "P3P" );
		static constexpr well_known_t HOST					= well_known_t( "Host" );
		static constexpr well_known_t DATE					= well_known_t( "Date" );
		static constexpr well_known_t ETAG					= well_known_t( "ETag" );
		static constexpr well_known_t VARY					= well_known_t( "Vary" );
		static constexpr well_known_t LINK					= well_known_t( "Link" );
		static constexpr well_known_t ALLOW					= well_known_t( "Allow" );
		static constexpr well_known_t RANGE					= well_known_t( "Range" );
		static constexpr well_known_t COOKIE				= well_known_t( "Cookie" );
		static constexpr well_known_t ACCEPT				= well_known_t( "Accept" );
		static constexpr well_known_t STATUS				= well_known_t( "Status" );
		static constexpr well_known_t SERVER				= well_known_t( "Server" );
		static constexpr well_known_t EXPECT				= well_known_t( "Expect" );
		static constexpr well_known_t PRAGMA				= well_known_t( "Pragma" );
		static constexpr well_known_t UPGRADE				= well_known_t( "Upgrade" );
		static constexpr well_known_t REFERER				= well_known_t( "Referer" );
		static constexpr well_known_t EXPIRES				= well_known_t( "Expires" );
		static constexpr well_known_t LOCATION				= well_known_t( "Location" );
		static constexpr well_known_t IF_MATCH				= well_known_t( "If-Match" );
		static constexpr well_known_t IF_RANGE				= well_known_t( "If-Range" );
		static constexpr well_known_t FORWARDED				= well_known_t( "Forwarded" );
		static constexpr well_known_t EXPECT_CT				= well_known_t( "Expect-CT" );
		static constexpr well_known_t CONNECTION			= well_known_t( "Connection" );
		static constexpr well_known_t SET_COOKIE			= well_known_t( "Set-Cookie" );
		static constexpr well_known_t USER_AGENT			= well_known_t( "User-Agent" );
		static constexpr well_known_t KEEP_ALIVE			= well_known_t( "Keep-Alive" );
		static constexpr well_known_t CONTENT_TYPE			= well_known_t( "Content-Type" );
		static constexpr well_known_t LAST_MODIFIED			= well_known_t( "Last-Modified" );
		static constexpr well_known_t AUTHORIZATION			= well_known_t( "Authorization" );
		static constexpr well_known_t IF_NONE_MATCH			= well_known_t( "If-None-Match" );
		static constexpr well_known_t CACHE_CONTROL			= well_known_t( "Cache-Control" );
		static constexpr well_known_t ACCEPT_RANGES			= well_known_t( "Accept-Ranges" );
		static constexpr well_known_t CONTENT_RANGE			= well_known_t( "Content-Range" );
		static constexpr well_known_t CONTENT_LENGTH		= well_known_t( "Content-Length" );
		static constexpr well_known_t ACCEPT_ENCODING		= well_known_t( "Accept-Encoding" );
		static constexpr well_known_t ACCEPT_LANGUAGE		= well_known_t( "Accept-Language" );
		static constexpr well_known_t REFERRER_POLICY		= well_known_t( "Referrer-Policy" );
		static constexpr well_known_t X_FORWARDED_FOR		= well_known_t( "X-Forwarded-For" );
		static constexpr well_known_t X_FRAME_OPTIONS		= well_known_t( "X-Frame-Options" );
		static constexpr well_known_t WWW_AUTHENTICATE		= well_known_t( "WWW-Authenticate" );
		static constexpr well_known_t CONTENT_ENCODING		= well_known_t( "Content-Encoding" );
		static constexpr well_known_t CONTENT_LOCATION		= well_known_t( "Content-Location" );
		static constexpr well_known_t X_XSS_PROTECTION		= well_known_t( "X-XSS-Protection" );
		static constexpr well_known_t IF_MODIFIED_SINCE		= well_known_t( "If-Modified-Since" );
		static constexpr well_known_t TRANSFER_ENCODING		= well_known_t( "Transfer-Encoding" );
		static constexpr well_known_t X_FORWARDED_PROTO		= well_known_t( "X-Forwarded-Proto" );
		static constexpr well_known_t IF_UNMODIFIED_SINCE	= well_known_t( "If-Unmodified-Since" );

		/* Web-Socket handshaking headers */
		static constexpr well_known_t SEC_WEBSOCKET_KEY		= well_known_t("Sec-WebSocket-Key");
		static constexpr well_known_t SEC_WEBSOCKET_ACCEPT	= well_known_t("Sec-WebSocket-Accept");

	private:
		std::string name;
		std::string value;

	public:
		http_header() { }
		http_header(const well_known_t& v) : name(v._1, v._len) { }
		http_header(const std::string& name, const std::string& value)
			: name(name), value(value) { qualify(); }

		http_header(const http_header& h) : name(h.name), value(h.value) { }
		http_header(http_header&& h) : name(std::move(h.name)), value(std::move(h.value)) { }

		inline http_header& operator =(const http_header& o) { name = o.name; value = o.value; return *this; }
		inline http_header& operator =(http_header&& o) { name = std::move(o.name); value = std::move(o.value); return *this; }

	public:
		inline operator bool() const { return name.size(); }
		inline bool operator !() const { return !name.size(); }

		inline bool operator ==(const http_header& m) const { return name.size() == m.name.size() && name == m.name && value.size() == m.value.size() && value == m.value; }
		inline bool operator !=(const http_header& m) const { return name.size() != m.name.size() || name != m.name || value.size() != m.value.size() || value != m.value; }

		inline bool operator ==(const well_known_t& w) const { return name.size() == w._len && name == w._1; }
		inline bool operator !=(const well_known_t& w) const { return name.size() != w._len || name != w._1; }

	public:
		/**
		 * parse one header from http request.
		 * @param by_receiving for parsing resource header in receiving loop.
		 * @returns
		 *	1. >  0: success.
		 *  2. =  0: incompleted.
		 *  3. = -1: invalid string.
		 */
		static int32_t try_parse(http_header& dst, const char* src, size_t max, bool by_receiving = false);

		/**
		 * stringify header to generate http request.
		 * @returns false if name not set.
		 */
		bool stringify(std::string& out_string, bool with_crlf = true) const;

	public:
		inline const std::string& get_name() const  { return name; }
		inline const std::string& get_value() const { return value; }

		inline void set_name(const std::string& name) { this->name = name; }
		inline void set_value(const std::string& value) { this->value = value; }

	private:
		/* trim whitespaces. */
		inline void qualify() {
			name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), name.end());
			name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char ch) { return !std::isspace(ch); }));
			
			value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
			value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
		}

	public:
		/**
		 * get kv value. 
		 * this parses ';' and '=' seperated KV value.
		 */
		inline bool get_kv_value(std::string& out_value, const nstring& key, char seperator = ';') const {
			size_t pos = value.find_first_of(key.string, 0, key.length);

			if (pos != std::string::npos) {
				pos += key.length;

				while(pos < value.size() && std::isspace(value[pos]))
					++pos;

				if (pos >= value.size())
					return false;

				if (value[pos++] == '=') {
					while (pos < value.size() && std::isspace(value[pos]))
						 ++pos;

					if (pos >= value.size())
						return true;

					size_t scol = value.find_first_of(seperator, pos);
					if (scol == std::string::npos) scol = value.size();

					--scol;
					while (pos < scol && std::isspace(value[scol]))
						--scol;

					// pos ~ scol.
					if (pos != scol)
						out_value.append(value.c_str(), scol - pos);

					return true;
				}
			}

			return false;
		}
	};

}