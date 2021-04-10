#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"
#include <ctime>

namespace nhttp {

	/**
	 * class http_date.
	 * represents timestamp in http style.
	 * e.g. Wed, 31 Mar 2021 07:28:00 GMT
	 */
	class NHTTP_API http_date {
	private:
		time_t timestamp; // Local timestamp.

	public:
		http_date() : timestamp(time(0)) { }
		http_date(time_t timestamp) : timestamp(timestamp) { }
		
	public:
		/**
		 * parse date string from http request.
		 * @param by_receiving for parsing resource header in receiving loop.
		 * @returns
		 *	1. >  0: success.
		 *  2. =  0: incompleted.
		 *  3. = -1: invalid string or not supported time-zone.
		 */
		static int32_t try_parse(http_date& dst, const char* src, ssize_t max = -1, bool by_receiving = true);

		bool stringify(std::string& out_string) const;

		inline std::string stringify() const {
			std::string out;
			stringify(out);
			return out;
		}

		/* get timestamp. */
		inline time_t get_timestamp() const { return timestamp; }
		inline time_t get_gmt_timestamp() const { return timestamp + get_time_delta(); }

	private:
		inline static time_t get_time_delta() {
			time_t now = time(nullptr), lsec, gsec;
			lsec = mktime(localtime(&now));
			gsec = mktime(gmtime(&now));
			return gsec - lsec;
		}

	public:
		inline bool operator ==(const http_date& t) const { return timestamp == t.timestamp; }
		inline bool operator !=(const http_date& t) const { return timestamp != t.timestamp; }

		inline bool operator >=(const http_date& t) const { return timestamp >= t.timestamp; }
		inline bool operator <=(const http_date& t) const { return timestamp <= t.timestamp; }

		inline bool operator > (const http_date& t) const { return timestamp >  t.timestamp; }
		inline bool operator < (const http_date& t) const { return timestamp <  t.timestamp; }
	};

}