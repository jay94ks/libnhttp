#pragma once

// internal implementation detail — never installed, never included from
// include/nhttp/**.

#include <winsock2.h>

namespace nhttp::platform::win32_detail {

	/* WSAStartup, called exactly once (C++11 magic statics), before any
	 * Winsock entry point is used — every win32 platform .cpp calls this
	 * first thing in any function that touches sockets/name resolution. */
	inline void ensure_wsa_started() noexcept {
		static const int result = [] {
			WSADATA data{};
			return ::WSAStartup(MAKEWORD(2, 2), &data);
		}();

		(void)result;
	}

}
