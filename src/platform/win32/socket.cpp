#include "sockaddr_convert.hpp"
#include "wsa_init.hpp"
#include "nhttp/platform/socket.hpp"

#include <climits>
#include <mswsock.h>
#include <utility>

namespace nhttp::platform {

	bool would_block() noexcept {
		return ::WSAGetLastError() == WSAEWOULDBLOCK;
	}

	bool was_interrupted() noexcept {
		return ::WSAGetLastError() == WSAEINTR;
	}

	int last_socket_error() noexcept {
		return ::WSAGetLastError();
	}

	std::string describe_socket_error(int code) {
		char buf[256] = { 0 };

		const DWORD written = ::FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, static_cast<DWORD>(code), 0, buf, sizeof(buf), nullptr);

		if (written == 0)
			return "winsock error " + std::to_string(code);

		return std::string(buf, written);
	}

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
		win32_detail::ensure_wsa_started();

		const int family = version == ip_version::v4 ? AF_INET : AF_INET6;
		const int type = proto == transport::tcp ? SOCK_STREAM : SOCK_DGRAM;

		const SOCKET s = ::socket(family, type, 0);

		if (s == INVALID_SOCKET)
			return socket_handle();

		return socket_handle(static_cast<native_socket_t>(s));
	}

	native_socket_t socket_handle::release() noexcept {
		return std::exchange(fd_, invalid_native_socket);
	}

	void socket_handle::close() noexcept {
		if (fd_ != invalid_native_socket) {
			::closesocket(static_cast<SOCKET>(fd_));
			fd_ = invalid_native_socket;
		}
	}

	bool socket_handle::set_nonblocking(bool enabled) const noexcept {
		u_long mode = enabled ? 1 : 0;
		return ::ioctlsocket(static_cast<SOCKET>(fd_), FIONBIO, &mode) == 0;
	}

	bool socket_handle::set_reuse_address(bool enabled) const noexcept {
		const int value = enabled ? 1 : 0;
		return ::setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_REUSEADDR,
			reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
	}

	bool socket_handle::set_reuse_port(bool) const noexcept {
		// Windows has no SO_REUSEPORT equivalent for inbound TCP accept
		// load-balancing across independently-bound listening sockets — unlike
		// Linux, binding N sockets to the same port here does not give kernel-
		// balanced accepts. listener::listen()/listen_tls() detect this (this
		// always returning false) and fall back to a single accept loop that
		// explicitly round-robins new connections across workers instead —
		// see CLAUDE.md's Windows-support notes.
		return false;
	}

	bool socket_handle::set_nodelay(bool enabled) const noexcept {
		const int value = enabled ? 1 : 0;
		return ::setsockopt(static_cast<SOCKET>(fd_), IPPROTO_TCP, TCP_NODELAY,
			reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
	}

	bool socket_handle::set_keepalive(bool enabled) const noexcept {
		const int value = enabled ? 1 : 0;
		return ::setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_KEEPALIVE,
			reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
	}

	bool socket_handle::set_linger(bool enabled, int timeout_seconds) const noexcept {
		linger l{};
		l.l_onoff = enabled ? 1 : 0;
		l.l_linger = static_cast<u_short>(timeout_seconds);
		return ::setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_LINGER,
			reinterpret_cast<const char*>(&l), sizeof(l)) == 0;
	}

	bool socket_handle::bind(const endpoint& ep) const noexcept {
		sockaddr_storage storage{};
		const int len = win32_detail::endpoint_to_sockaddr(ep, storage);

		return ::bind(static_cast<SOCKET>(fd_), reinterpret_cast<const sockaddr*>(&storage), len) == 0;
	}

	bool socket_handle::listen(int backlog) const noexcept {
		return ::listen(static_cast<SOCKET>(fd_), backlog) == 0;
	}

	std::optional<std::pair<socket_handle, endpoint>> socket_handle::accept() const noexcept {
		sockaddr_storage storage{};
		int len = sizeof(storage);

		const SOCKET accepted = ::accept(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&storage), &len);

		if (accepted == INVALID_SOCKET)
			return std::nullopt;

		auto ep = win32_detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&storage), len);

		if (!ep) {
			::closesocket(accepted);
			return std::nullopt;
		}

		return std::make_pair(socket_handle(static_cast<native_socket_t>(accepted)), *ep);
	}

	connect_result socket_handle::connect(const endpoint& ep) const noexcept {
		sockaddr_storage storage{};
		const int len = win32_detail::endpoint_to_sockaddr(ep, storage);

		if (::connect(static_cast<SOCKET>(fd_), reinterpret_cast<const sockaddr*>(&storage), len) == 0)
			return connect_result::connected;

		if (::WSAGetLastError() == WSAEWOULDBLOCK)
			return connect_result::in_progress;

		return connect_result::failed;
	}

	int socket_handle::socket_error() const noexcept {
		int value = 0;
		int len = sizeof(value);

		if (::getsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&value), &len) != 0)
			return ::WSAGetLastError();

		return value;
	}

	std::int64_t socket_handle::read(void* buf, std::size_t n) const noexcept {
		const int capped = static_cast<int>(n > static_cast<std::size_t>(INT_MAX) ? static_cast<std::size_t>(INT_MAX) : n);
		return ::recv(static_cast<SOCKET>(fd_), static_cast<char*>(buf), capped, 0);
	}

	std::int64_t socket_handle::write(const void* buf, std::size_t n) const noexcept {
		const int capped = static_cast<int>(n > static_cast<std::size_t>(INT_MAX) ? static_cast<std::size_t>(INT_MAX) : n);
		return ::send(static_cast<SOCKET>(fd_), static_cast<const char*>(buf), capped, 0);
	}

	bool socket_handle::supports_send_file() noexcept {
		// TransmitFile is Windows' sendfile(2) equivalent, but only offers a
		// non-blocking-friendly form via OVERLAPPED I/O + IOCP completion —
		// this reactor's plain read/write path deliberately stays on the
		// simpler WSAEventSelect-then-retry model (see CLAUDE.md's Phase 12
		// design note), which TransmitFile doesn't fit without a real
		// completion-based driver. Deferred; see PLAN.md's P1.
		return false;
	}

	std::int64_t socket_handle::send_file(int, std::int64_t&, std::size_t) const noexcept {
		// never called: supports_send_file() is false on this platform, and
		// every caller is required to check that first.
		::WSASetLastError(WSAEOPNOTSUPP);
		return -1;
	}

	bool socket_handle::shutdown_both() const noexcept {
		return ::shutdown(static_cast<SOCKET>(fd_), SD_BOTH) == 0;
	}

	std::optional<endpoint> socket_handle::local_endpoint() const noexcept {
		sockaddr_storage storage{};
		int len = sizeof(storage);

		if (::getsockname(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&storage), &len) != 0)
			return std::nullopt;

		return win32_detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&storage), len);
	}

	std::optional<endpoint> socket_handle::remote_endpoint() const noexcept {
		sockaddr_storage storage{};
		int len = sizeof(storage);

		if (::getpeername(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&storage), &len) != 0)
			return std::nullopt;

		return win32_detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&storage), len);
	}

}
