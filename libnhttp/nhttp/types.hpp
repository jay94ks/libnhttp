#pragma once
#ifdef _MSC_VER
#	define _CRT_SECURE_NO_WARNINGS
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>

#if !(defined(_WIN64) || defined(_WIN32))
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <endian.h>
#endif

#include <cstdint>
#include <vector>
#include <queue>
#include <map>
#include <list>
#include <atomic>
#include <mutex>
#include <regex>

#ifdef _MSC_VER
#	define inline __forceinline
#	define __nhttp_packed__

#if defined(__NHTTP_STATIC__)
#	define NHTTP_API
#else
#if defined(__NHTTP_BUILD__)
#	define NHTTP_API __declspec(dllexport)
#else
#	define NHTTP_API __declspec(dllimport)
#endif
#endif
#else
//#	define inline __attribute__((always_inline))
#	define NHTTP_API
#	define __nhttp_packed__  __attribute__((packed))
#endif

#if defined(_DEBUG)
#	define NHTTP_DEBUG_ENABLED	1
#	define NHTTP_DEBUG(...) __VA_ARGS__
#	define NHTTP_RELEASE(...)
#else
#	define NHTTP_DEBUG_ENABLED	0
#	define NHTTP_DEBUG(...)
#	define NHTTP_RELEASE(...) __VA_ARGS__
#endif

namespace nhttp {
	using nullptr_t = decltype(nullptr);

#if defined(_WIN64) || defined(_WIN32)
	using ssize_t = typename std::make_signed<size_t>::type;
#endif

	/* alternative of <bit> header. */
	enum endianness {
		NHTTP_ENDIAN_LITTLE	= 0,	/* Little endian. */
		NHTTP_ENDIAN_BIG	= 1,	/* Big endian.    */

#if defined(_WIN64) || defined(_WIN32)
		NHTTP_ENDIAN_NATIVE	= NHTTP_ENDIAN_LITTLE
#	define NHTTP_LITTLE_ENDIAN	1
#else
#	if __BYTE_ORDER == __LITTLE_ENDIAN
		NHTTP_ENDIAN_NATIVE = NHTTP_ENDIAN_LITTLE
#	define NHTTP_LITTLE_ENDIAN	1
#	elif __BYTE_ORDER == __BIG_ENDIAN
		NHTTP_ENDIAN_NATIVE = NHTTP_ENDIAN_BIG
#	define NHTTP_BIG_ENDIAN		1
#	else
#	pragma error "Not supported endian."
#	endif
#endif

#ifndef NHTTP_LITTLE_ENDIAN
#	define NHTTP_LITTLE_ENDIAN	0
#endif
#ifndef NHTTP_BIG_ENDIAN
#	define NHTTP_BIG_ENDIAN		0
#endif
	};
}
