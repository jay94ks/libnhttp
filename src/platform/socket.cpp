#include "nhttp/platform/socket.hpp"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace nhttp::platform {

	socket_handle& socket_handle::operator=(socket_handle&& other) noexcept {
		if (this != &other) {
			close();
			fd_ = other.release();
		}

		return *this;
	}

	socket_handle::~socket_handle() {
		close();
	}

	socket_handle socket_handle::create(ip_version version, transport proto) noexcept {
		const int family = version == ip_version::v4 ? AF_INET : AF_INET6;
		const int type = proto == transport::tcp ? SOCK_STREAM : SOCK_DGRAM;

		return socket_handle(::socket(family, type, 0));
	}

	int socket_handle::release() noexcept {
		return std::exchange(fd_, -1);
	}

	void socket_handle::close() noexcept {
		if (fd_ >= 0) {
			::close(fd_);
			fd_ = -1;
		}
	}

	bool socket_handle::set_nonblocking(bool enabled) const noexcept {
		const int flags = ::fcntl(fd_, F_GETFL, 0);
		if (flags < 0)
			return false;

		const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
		return ::fcntl(fd_, F_SETFL, updated) == 0;
	}

	bool socket_handle::set_reuse_address(bool enabled) const noexcept {
		const int value = enabled ? 1 : 0;
		return ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == 0;
	}

	bool socket_handle::set_reuse_port(bool enabled) const noexcept {
		const int value = enabled ? 1 : 0;
		return ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &value, sizeof(value)) == 0;
	}

	bool socket_handle::set_nodelay(bool enabled) const noexcept {
		const int value = enabled ? 1 : 0;
		return ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) == 0;
	}

	bool socket_handle::set_keepalive(bool enabled) const noexcept {
		const int value = enabled ? 1 : 0;
		return ::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof(value)) == 0;
	}

	bool socket_handle::set_linger(bool enabled, int timeout_seconds) const noexcept {
		linger l{};
		l.l_onoff = enabled ? 1 : 0;
		l.l_linger = timeout_seconds;
		return ::setsockopt(fd_, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) == 0;
	}

	bool socket_handle::bind(const endpoint& ep) const noexcept {
		sockaddr_storage storage{};
		const socklen_t len = ep.to_sockaddr(storage);

		return ::bind(fd_, reinterpret_cast<const sockaddr*>(&storage), len) == 0;
	}

	bool socket_handle::listen(int backlog) const noexcept {
		return ::listen(fd_, backlog) == 0;
	}

	int socket_handle::accept_raw(sockaddr_storage& out_addr, socklen_t& out_len) const noexcept {
		out_len = sizeof(out_addr);
		return ::accept(fd_, reinterpret_cast<sockaddr*>(&out_addr), &out_len);
	}

	connect_result socket_handle::connect_raw(const endpoint& ep) const noexcept {
		sockaddr_storage storage{};
		const socklen_t len = ep.to_sockaddr(storage);

		if (::connect(fd_, reinterpret_cast<const sockaddr*>(&storage), len) == 0)
			return connect_result::connected;

		if (errno == EINPROGRESS)
			return connect_result::in_progress;

		return connect_result::failed;
	}

	int socket_handle::socket_error() const noexcept {
		int value = 0;
		socklen_t len = sizeof(value);

		if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &value, &len) != 0)
			return errno;

		return value;
	}

	ssize_t socket_handle::read(void* buf, std::size_t n) const noexcept {
		return ::read(fd_, buf, n);
	}

	ssize_t socket_handle::write(const void* buf, std::size_t n) const noexcept {
		return ::write(fd_, buf, n);
	}

	bool socket_handle::shutdown_both() const noexcept {
		return ::shutdown(fd_, SHUT_RDWR) == 0;
	}

	std::optional<endpoint> socket_handle::local_endpoint() const noexcept {
		sockaddr_storage storage{};
		socklen_t len = sizeof(storage);

		if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&storage), &len) != 0)
			return std::nullopt;

		return endpoint::from_sockaddr(reinterpret_cast<const sockaddr*>(&storage), len);
	}

	std::optional<endpoint> socket_handle::remote_endpoint() const noexcept {
		sockaddr_storage storage{};
		socklen_t len = sizeof(storage);

		if (::getpeername(fd_, reinterpret_cast<sockaddr*>(&storage), &len) != 0)
			return std::nullopt;

		return endpoint::from_sockaddr(reinterpret_cast<const sockaddr*>(&storage), len);
	}

}
