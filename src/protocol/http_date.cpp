#include "nhttp/protocol/http_date.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace nhttp::protocol {

	namespace {

		/* gmtime_r/timegm are POSIX; MSVC's equivalents take a different
		 * argument order (gmtime_s) or a different name (_mkgmtime) — wrapped
		 * here so the rest of this file stays platform-neutral. */
		std::tm* portable_gmtime(const std::time_t* time, std::tm* out) noexcept {
#if defined(_WIN32)
			return ::gmtime_s(out, time) == 0 ? out : nullptr;
#else
			return ::gmtime_r(time, out);
#endif
		}

		std::time_t portable_timegm(std::tm* tm) noexcept {
#if defined(_WIN32)
			return ::_mkgmtime(tm);
#else
			return ::timegm(tm);
#endif
		}

		constexpr std::array<const char*, 7> weekday_names{
			"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
		};

		constexpr std::array<const char*, 12> month_names{
			"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
		};

		int month_index(std::string_view name) noexcept {
			for (std::size_t i = 0; i < month_names.size(); ++i) {
				if (name == month_names[i])
					return static_cast<int>(i);
			}

			return -1;
		}

	}

	std::string format_http_date(std::time_t time) {
		std::tm tm{};
		portable_gmtime(&time, &tm);

		char buf[32];
		const int n = std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
			weekday_names[static_cast<std::size_t>(tm.tm_wday)],
			tm.tm_mday,
			month_names[static_cast<std::size_t>(tm.tm_mon)],
			tm.tm_year + 1900,
			tm.tm_hour, tm.tm_min, tm.tm_sec);

		return std::string(buf, static_cast<std::size_t>(n > 0 ? n : 0));
	}

	std::time_t parse_http_date(std::string_view text) noexcept {
		// RFC 1123 form only: "Tue, 15 Nov 1994 08:12:31 GMT".
		if (text.size() < 29)
			return static_cast<std::time_t>(-1);

		const std::string_view mday = text.substr(5, 2);
		const std::string_view mon = text.substr(8, 3);
		const std::string_view year = text.substr(12, 4);
		const std::string_view hour = text.substr(17, 2);
		const std::string_view min = text.substr(20, 2);
		const std::string_view sec = text.substr(23, 2);

		const int month = month_index(mon);

		if (month < 0)
			return static_cast<std::time_t>(-1);

		auto to_int = [](std::string_view digits) noexcept -> int {
			int value = 0;

			for (const char c : digits) {
				if (c < '0' || c > '9')
					return -1;

				value = value * 10 + (c - '0');
			}

			return value;
		};

		std::tm tm{};
		tm.tm_mday = to_int(mday);
		tm.tm_mon = month;
		tm.tm_year = to_int(year) - 1900;
		tm.tm_hour = to_int(hour);
		tm.tm_min = to_int(min);
		tm.tm_sec = to_int(sec);

		if (tm.tm_mday < 0 || tm.tm_hour < 0 || tm.tm_min < 0 || tm.tm_sec < 0)
			return static_cast<std::time_t>(-1);

		return portable_timegm(&tm);
	}

}
