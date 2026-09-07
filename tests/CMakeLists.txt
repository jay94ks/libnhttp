include(FetchContent)

FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.7.1
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

# Explicit source list (not GLOB) — add new test files here as phases land.
set(NHTTP_UNIT_TEST_SOURCES
	unit/test_version.cpp
	unit/test_task.cpp
	unit/test_mpmc_queue.cpp
	unit/test_platform_socket.cpp
	unit/test_io_context.cpp
	unit/test_thread_pool.cpp
	unit/test_io_context_pool.cpp
	unit/test_memory_stream.cpp
	unit/test_range_stream.cpp
	unit/test_file_stream.cpp
	unit/test_socket_stream.cpp
	unit/test_http_header.cpp
	unit/test_http_method.cpp
	unit/test_http_status.cpp
	unit/test_urlencode.cpp
	unit/test_http_query_string.cpp
	unit/test_http_date.cpp
	unit/test_http_mime_type.cpp
	unit/test_http_resource.cpp
	unit/test_http_chunked.cpp
	unit/test_http_multipart.cpp
	integration/test_server_basic.cpp
	integration/test_server_extensions.cpp
	integration/test_reverse_proxy.cpp
	integration/test_http2_server.cpp
	unit/test_router_route.cpp
	integration/test_router_server.cpp
	unit/test_ws_sha1_handshake.cpp
	unit/test_ws_connection.cpp
	integration/test_websocket_server.cpp
	unit/test_hpack.cpp
)

if(NHTTP_ENABLE_TLS)
	list(APPEND NHTTP_UNIT_TEST_SOURCES integration/test_tls_server.cpp)
endif()

add_executable(nhttp_unit_tests ${NHTTP_UNIT_TEST_SOURCES})

target_link_libraries(nhttp_unit_tests PRIVATE
	nhttp::nhttp
	nhttp_warnings
	Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(nhttp_unit_tests)
