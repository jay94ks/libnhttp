#pragma once

#include "address.hpp"

#include <cstddef>
#include <sys/types.h>

namespace nhttp::platform {

	enum class transport { tcp, udp };

	enum class connect_result { connected, in_progress, failed };

	/**
	 * class socket_handle.
	 * RAII owner of one native socket file descriptor. every operation is
	 * non-blocking-oriented: callers (the async layer) are expected to react to
	 * EAGAIN/EWOULDBLOCK by waiting for readiness and retrying, not by blocking here.
	 */
	class socket_handle {
	public:
		socket_handle() noexcept = default;
		explicit socket_handle(int fd) noexcept : fd_(fd) { }

		socket_handle(socket_handle&& other) noexcept : fd_(other.release()) { }
		socket_handle& operator=(socket_handle&& other) noexcept;

		socket_handle(const socket_handle&) = delete;
		socket_handle& operator=(const socket_handle&) = delete;

		~socket_handle();

	public:
		static socket_handle create(ip_version version, transport proto) noexcept;

		bool valid() const noexcept { return fd_ >= 0; }
		int native_handle() const noexcept { return fd_; }

		/* releases ownership of the fd without closing it. */
		int release() noexcept;

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

		/* returns the accepted fd, or -1 with errno set (EAGAIN/EWOULDBLOCK/EINTR expected). */
		int accept_raw(sockaddr_storage& out_addr, socklen_t& out_len) const noexcept;

		connect_result connect_raw(const endpoint& ep) const noexcept;

		/* reads SO_ERROR; 0 means the socket is healthy / a pending connect succeeded. */
		int socket_error() const noexcept;

		ssize_t read(void* buf, std::size_t n) const noexcept;
		ssize_t write(const void* buf, std::size_t n) const noexcept;

		bool shutdown_both() const noexcept;

		std::optional<endpoint> local_endpoint() const noexcept;
		std::optional<endpoint> remote_endpoint() const noexcept;

	private:
		int fd_ = -1;
	};

}
