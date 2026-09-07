#pragma once

#include "address.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace nhttp::platform {

	/**
	 * the native socket handle type: a POSIX file descriptor is a small `int`;
	 * a Windows `SOCKET` is pointer-sized and unsigned (INVALID_SOCKET is
	 * `(SOCKET)-1`, not a negative int). `_WIN32` is a compiler-predefined
	 * macro, so branching on it here needs no OS header at all.
	 */
#if defined(_WIN32)
	using native_socket_t = std::uintptr_t;
	inline constexpr native_socket_t invalid_native_socket = static_cast<native_socket_t>(-1);
#else
	using native_socket_t = int;
	inline constexpr native_socket_t invalid_native_socket = -1;
#endif

	enum class transport { tcp, udp };

	enum class connect_result { connected, in_progress, failed };

	/* portable stand-ins for checking a failed read/write/accept/connect's cause,
	 * without naming errno/WSAGetLastError or their header in this public header. */
	bool would_block() noexcept;
	bool was_interrupted() noexcept;
	int last_socket_error() noexcept;
	std::string describe_socket_error(int code);

	/**
	 * class socket_handle.
	 * RAII owner of one native socket. every operation is non-blocking-oriented:
	 * callers (the async layer) are expected to react to would_block() by
	 * waiting for readiness and retrying, not by blocking here.
	 */
	class socket_handle {
	public:
		socket_handle() noexcept = default;
		explicit socket_handle(native_socket_t fd) noexcept : fd_(fd) { }

		socket_handle(socket_handle&& other) noexcept : fd_(other.release()) { }
		socket_handle& operator=(socket_handle&& other) noexcept;

		socket_handle(const socket_handle&) = delete;
		socket_handle& operator=(const socket_handle&) = delete;

		~socket_handle();

	public:
		static socket_handle create(ip_version version, transport proto) noexcept;

		bool valid() const noexcept { return fd_ != invalid_native_socket; }
		native_socket_t native_handle() const noexcept { return fd_; }

		/* releases ownership of the fd without closing it. */
		native_socket_t release() noexcept;

		void close() noexcept;

	public:
		bool set_nonblocking(bool enabled) const noexcept;
		bool set_reuse_address(bool enabled) const noexcept;
		bool set_reuse_port(bool enabled) const noexcept;
		bool set_nodelay(bool enabled) const noexcept;
		bool set_keepalive(bool enabled) const noexcept;
		bool set_linger(bool enabled, int timeout_seconds) const noexcept;

	public:
		bool bind(const endpoint& ep) const noexcept;
		bool listen(int backlog) const noexcept;

		/* nullopt + would_block()/was_interrupted() are the expected non-fatal cases. */
		std::optional<std::pair<socket_handle, endpoint>> accept() const noexcept;

		connect_result connect(const endpoint& ep) const noexcept;

		/* reads SO_ERROR; 0 means the socket is healthy / a pending connect succeeded. */
		int socket_error() const noexcept;

		std::int64_t read(void* buf, std::size_t n) const noexcept;
		std::int64_t write(const void* buf, std::size_t n) const noexcept;

		bool shutdown_both() const noexcept;

		std::optional<endpoint> local_endpoint() const noexcept;
		std::optional<endpoint> remote_endpoint() const noexcept;

	private:
		native_socket_t fd_ = invalid_native_socket;
	};

}
