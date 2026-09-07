cmake_minimum_required(VERSION 3.20)

project(nhttp
	VERSION 2.0.0
	DESCRIPTION "Event-driven, coroutine-based HTTP server library for C++20"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32)
	message(WARNING
		"nhttp targets Linux (epoll) and Windows (IOCP) only. "
		"Configuring on '${CMAKE_SYSTEM_NAME}' is unsupported and will not build."
	)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
	message(FATAL_ERROR "nhttp requires GCC >= 11 for C++20 coroutine support (found ${CMAKE_CXX_COMPILER_VERSION}).")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
	message(FATAL_ERROR "nhttp requires Clang >= 14 for C++20 coroutine support (found ${CMAKE_CXX_COMPILER_VERSION}).")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.29)
	message(FATAL_ERROR "nhttp requires MSVC >= 19.29 (VS 2019 16.10) for C++20 coroutine support (found ${CMAKE_CXX_COMPILER_VERSION}).")
endif()

option(NHTTP_BUILD_TESTS "Build the nhttp unit/integration test suite" ON)
option(NHTTP_BUILD_EXAMPLES "Build the nhttp example applications" ON)
option(NHTTP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(NHTTP_ENABLE_TLS "Build TLS/SSL support (requires OpenSSL)" ON)

include(cmake/CompilerWarnings.cmake)

if(NHTTP_ENABLE_TLS)
	find_package(OpenSSL)

	if(NOT OpenSSL_FOUND)
		message(FATAL_ERROR
			"NHTTP_ENABLE_TLS is ON but OpenSSL was not found. "
			"Install libssl-dev (Debian/Ubuntu) or the equivalent, or configure with -DNHTTP_ENABLE_TLS=OFF."
		)
	endif()
endif()

add_subdirectory(src)

if(NHTTP_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(NHTTP_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
	add_subdirectory(examples)
endif()
