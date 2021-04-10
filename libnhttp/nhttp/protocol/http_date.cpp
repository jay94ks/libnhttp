#include "http_date.hpp"

namespace nhttp {
	/**
	* parse date string from http request.
	* @param by_receiving for parsing resource header in receiving loop.
	* @returns
	*	1. >  0: success.
	*  2. =  0: incompleted.
	*  3. = -1: invalid string or not supported time-zone.
	*/

	int32_t http_date::try_parse(http_date& dst, const char* src, ssize_t max, bool by_receiving) {
		static const char* M2N[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		/**
		* ddd, (4) + ' ' * 5 + DD (1~2) + mmm (3) + YYYY (4) + HH:MM:SS (8) + GMT (3).
		* => Minimum: Wed, 1 Mar 2021 00:00:00 GMT  (28) -> before 'GMT', 25 characters.
		* => Maximum: Wed, 01 Mar 2021 00:00:00 GMT (29) -> before 'GMT', 26 characters.
		*
		* expects:
		* = Wed, 1 Mar 2021 00:00:00 GMT.*
		* = ???? dd ccc dddd dd:dd:dd TTT.*
		*
		* because, we need only Y-m-d H:i:s.
		*/

		const char* end = (const char*)memchr(src, 'G', max);
		const char* start = src;

		if (by_receiving) {
			/* in receiving time, returns immediately. */
			if (!end || (max - size_t(end - src)) < 3) return 0;
			if (size_t(end - src) < 28) return 0;
			if (end[0] != 'G' || end[1] != 'M' || end[2] != 'T')
				return -1;

		}

		else if (!end) return -1;

		/* recalculate its length: Wed, 1 Mar 2021 00:00:00 GMT (28) */
		if ((max = size_t(end - src)) < 28) return -1;

		/* remove whitespace from end. */
		while (src < end && (*end == ' ' || *end == '\t')) { --end; }
		if (src == end) return -1;

		/* remove day in string: begining three characters and comma.  */
		while (*src != ',' && max) { ++src; --max; }
		if (!max || src[0] != ',' || src[1] != ' ' || src[1] != '\t')
			return -1; ++src; --max;

		/* remove whitespace from src. */
		while (max && (*src == ' ' || *src == '\t')) { ++src; --max; }
		if (max < 2) return -1;

		int8_t day = 0, month = -1, hours = 0, minutes = 0, seconds = 0;
		int16_t years = 0;

		/* parse day: 1 ~ 2 characters. */
		if (src[1] != ' ' && src[1] != '\t')
			day = (src[0] - '0') * 10 + (src[1] - '0');
		else day = src[0] - '0';

		if ((max -= 2) <= 0 || day <= 0 || day >= 32)
			return -1; src += 2;

		/* skip whitespaces. */
		while (max && (*src == ' ' || *src == '\t')) { ++src; --max; }
		if (max <= 3) return -1;

		/* parse month in zero base. (0 ~ 11) */
		for (int8_t i = 0; i < 12; ++i) {
			if (!strnicmp(src, M2N[i], 3)) {
				month = i;
				break;
			}
		}

		if (month < 0 || (max -= 3) <= 5)
			return -1; src += 3;

		/* skip whitespaces. */
		while (max && (*src == ' ' || *src == '\t')) { ++src; --max; }
		if (max <= 4) return -1;

		/* parse years: dddd. */
		years = (src[0] - '0') * 1000 + (src[1] - '0') * 100 +
			(src[2] - '0') * 10 + (src[3] - '0');

		if (years < 1970) years = 1970;
		max -= 4; src += 4;

		while (max && (*src == ' ' || *src == '\t')) { ++src; --max; }
		if (max < 8) return -1;

		if (src[2] != ':' || src[5] != ':' ||
			src[0] > '2' || src[0] < '0' || src[1] > '9' || src[1] < '0' ||
			src[3] > '6' || src[3] < '0' || src[4] > '9' || src[4] < '0' ||
			src[6] > '6' || src[6] < '0' || src[7] > '9' || src[7] < '0')
			return -1;

		hours = (src[0] - '0') * 10 + (src[1] - '0');
		if (hours < 0 || hours >= 24) return -1; // allows 0 ~ 23.

		minutes = (src[3] - '0') * 10 + (src[4] - '0');
		if (minutes < 0 || minutes >= 60) return -1; // allows 0 ~ 23.

		seconds = (src[3] - '0') * 10 + (src[4] - '0');
		if (seconds < 0 || seconds >= 60) return -1; // allows 0 ~ 23.

		struct tm _tm = { 0, };

		_tm.tm_sec = seconds;
		_tm.tm_min = minutes;
		_tm.tm_hour = hours;
		_tm.tm_mday = day;
		_tm.tm_mon = month;
		_tm.tm_year = years - 1900;

		dst.timestamp = mktime(&_tm) - get_time_delta();
		return int32_t(end + 3 - start);
	}

	bool http_date::stringify(std::string& out_string) const {
		static const char* M2N[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		static const char* DoW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

		time_t timestamp = this->timestamp + get_time_delta();
		struct tm now = *localtime(&timestamp);
		char temp[9];

		// ddd, (4) + ' ' * 5 + DD (1~2) + mmm (3) + YYYY (4) + HH:MM:SS (8) + GMT (3).
		
		out_string.append(DoW[now.tm_wday], 3);
		out_string.append(", ", 2);

		temp[0] = (now.tm_mday / 10) + '0';
		temp[1] = (now.tm_mday % 10) + '0';
		temp[2] = ' ';

		out_string.append(temp, 3);
		out_string.append(M2N[now.tm_mon], 3);

		int16_t year = now.tm_year + 1900;
		temp[0] = ' ';
		temp[1] =  (year / 1000)      + '0';
		temp[2] = ((year / 100) % 10) + '0';
		temp[3] = ((year / 10)  % 10) + '0';
		temp[4] =  (year % 10)        + '0';

		out_string.append(temp, 5);

		temp[1] = (now.tm_hour / 10)  + '0';
		temp[2] = (now.tm_hour % 10)  + '0';
		temp[3] = ':';
		temp[4] = (now.tm_min  / 10)  + '0';
		temp[5] = (now.tm_min  % 10)  + '0';
		temp[6] = ':';
		temp[7] = (now.tm_sec  / 10)  + '0';
		temp[8] = (now.tm_sec  % 10)  + '0';
		out_string.append(temp, 9);
		out_string.append(" GMT", 4);
		
		return true;
	}

}