# Explicit source list (not GLOB) so CMake reconfigures deterministically when files are added.
# platform/{posix,win32}/*.cpp implement the same portable headers under
# include/nhttp/platform/ — selected below by the target OS, never both.
set(NHTTP_SOURCES
	version.cpp
	async/io_context.cpp
	async/io_context_pool.cpp
	async/socket.cpp
	async/thread_pool.cpp
	io/stream.cpp
	io/memory_stream.cpp
	io/range_stream.cpp
	io/socket_stream.cpp
	io/file_stream.cpp
	protocol/http_method.cpp
	protocol/http_header.cpp
	protocol/http_status.cpp
	protocol/urlencode.cpp
	protocol/http_query_string.cpp
	protocol/http_date.cpp
	protocol/http_mime_type.cpp
	protocol/http_resource.cpp
	protocol/http_chunked.cpp
	protocol/http_multipart.cpp
	server/response.cpp
	server/connection.cpp
	server/connection_h2.cpp
	server/http1_io.cpp
	server/listener.cpp
	server/extension.cpp
	server/extensions/vpath.cpp
	server/extensions/vhost.cpp
	server/extensions/static_content.cpp
	server/extensions/overlay.cpp
	server/extensions/single_file.cpp
	server/extensions/reverse_proxy.cpp
	router/middleware.cpp
	router/group_proxy.cpp
	router/route.cpp
	router/router.cpp
	ws/sha1.cpp
	ws/handshake.cpp
	ws/connection.cpp
	server/extensions/websocket_endpoint.cpp
	http2/hpack.cpp
	http2/frame.cpp
)

if(WIN32)
	list(APPEND NHTTP_SOURCES
		platform/win32/address.cpp
		platform/win32/socket.cpp
		platform/win32/reactor.cpp
		platform/win32/file_info.cpp
		platform/win32/file_mapping.cpp
	)
else()
	list(APPEND NHTTP_SOURCES
		platform/posix/address.cpp
		platform/posix/socket.cpp
		platform/posix/reactor.cpp
		platform/posix/file_info.cpp
		platform/posix/file_mapping.cpp
	)
endif()

if(NHTTP_ENABLE_TLS)
	list(APPEND NHTTP_SOURCES
		tls/context.cpp
		tls/stream.cpp
	)
endif()

add_library(nhttp STATIC ${NHTTP_SOURCES})
add_library(nhttp::nhttp ALIAS nhttp)

target_compile_features(nhttp PUBLIC cxx_std_20)

target_include_directories(nhttp
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(nhttp PRIVATE nhttp_warnings)

if(WIN32)
	target_link_libraries(nhttp PUBLIC ws2_32 mswsock)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(nhttp PUBLIC Threads::Threads)
endif()

target_compile_definitions(nhttp PRIVATE
	NHTTP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
	NHTTP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
	NHTTP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
	NHTTP_VERSION_STRING="${PROJECT_VERSION}"
)

if(NHTTP_ENABLE_TLS)
	target_link_libraries(nhttp PUBLIC OpenSSL::SSL OpenSSL::Crypto)
	target_compile_definitions(nhttp PUBLIC NHTTP_HAVE_TLS)
endif()
