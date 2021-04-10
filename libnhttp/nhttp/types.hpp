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
}
