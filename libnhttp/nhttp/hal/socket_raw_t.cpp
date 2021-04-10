#if defined(_WIN32) || defined(_WIN64)
#if !defined(WIN32_LEAN_AND_MEAN)
#   define WIN32_LEAN_AND_MEAN
#endif

#if !defined(_CRT_SECURE_NO_DEPRECATE)
#   define _CRT_SECURE_NO_DEPRECATE
#endif

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <signal.h>
#include <cerrno>
#include <fcntl.h>
#endif

#include "socket_raw_t.hpp"

#if NHTTP_OS_WINDOWS
#   pragma comment(lib, "ws2_32.lib")
#endif

namespace nhttp {
namespace hal {

#if NHTTP_OS_WINDOWS
    class WSAInit {
    private:
        WSADATA _wsaData = { 0, };

    public:
        WSAInit() { ::WSAStartup(MAKEWORD(2, 0), &_wsaData); }
        ~WSAInit() { ::WSACleanup(); }
    };
#endif

    struct sockaddr_sr {
        union {
            sockaddr_in ipv4_addr;
            sockaddr_in6 ipv6_addr;
        };
    };

    inline void ipv4_to_sockaddr(const ipv4_addr& ip, sockaddr_sr& sa) {
        sa.ipv4_addr.sin_family = AF_INET;
        memcpy(&sa.ipv4_addr.sin_addr, ip.addr.u8_4, sizeof(uint32_t));
        sa.ipv4_addr.sin_port = htons(ip.port);
    }

    inline void ipv6_to_sockaddr(const ipv6_addr& ip, sockaddr_sr& sa) {
        sa.ipv6_addr.sin6_family = AF_INET6;
        memcpy(&sa.ipv6_addr.sin6_addr, ip.addr.u8_16, sizeof(uint32_t) * 4);
        sa.ipv6_addr.sin6_port = htons(ip.port);
        sa.ipv6_addr.sin6_flowinfo = htonl(ip.flow);
        sa.ipv6_addr.sin6_scope_id = htonl(ip.scope);
    }

    inline void sockaddr_to_ipv4(const sockaddr_sr& sa, ipv4_addr& ip) {
        memcpy(ip.addr.u8_4, &sa.ipv4_addr.sin_addr, sizeof(uint32_t));
        ip.port = ntohs(sa.ipv4_addr.sin_port);
    }

    inline void sockaddr_to_ipv6(const sockaddr_sr& sa, ipv6_addr& ip) {
        memcpy(ip.addr.u8_16, &sa.ipv6_addr.sin6_addr, sizeof(uint32_t) * 4);
        ip.port = ntohs(sa.ipv6_addr.sin6_port);
        ip.flow = ntohl(sa.ipv6_addr.sin6_flowinfo);
        ip.scope = ntohl(sa.ipv6_addr.sin6_scope_id);
    }

    NHTTP_API std::string to_string(const ipv4_addr& in) {
        char buf[32] = { 0, };
        sockaddr_sr sr = {0, };

        ipv4_to_sockaddr(in, sr);
        inet_ntop(AF_INET, &sr.ipv4_addr.sin_addr, buf, sizeof(buf));

        return buf;
    }

    NHTTP_API std::string to_string(const ipv6_addr& in) {
        char buf[128] = { 0, };
        sockaddr_sr sr = { 0, };

        ipv6_to_sockaddr(in, sr);
        inet_ntop(AF_INET6, &sr.ipv6_addr.sin6_addr, buf, sizeof(buf));

        return buf;
    }

    NHTTP_API bool resolve(ipv4_addr& out, const char* in_string) {
        sockaddr_sr sr = { 0, };

        sr.ipv4_addr.sin_family = AF_INET;

        if (inet_pton(AF_INET, in_string, &sr.ipv4_addr.sin_addr)) {
            sockaddr_to_ipv4(sr, out);
            return true;
        }

        addrinfo hint = {0, };
        addrinfo* results = nullptr;

        hint.ai_family = AF_INET;
        
        if (!getaddrinfo(in_string, nullptr, &hint, &results)) {
            bool found = false;

            for (addrinfo* rp = results; rp != nullptr; rp = rp->ai_next) {
                if (rp->ai_family == AF_INET) {
                    sr.ipv4_addr = *((sockaddr_in*)rp->ai_addr);
                    found = true;
                    break;
                }
            }

            freeaddrinfo(results);

            if (found) {
                sockaddr_to_ipv4(sr, out);
                return true;
            }
        }

        return false;
    }

    NHTTP_API bool resolve(ipv6_addr& out, const char* in_string) {
        sockaddr_sr sr = { 0, };

        sr.ipv6_addr.sin6_family = AF_INET;

        if (inet_pton(AF_INET6, in_string, &sr.ipv6_addr.sin6_addr)) {
            sockaddr_to_ipv6(sr, out);
            return true;
        }

        addrinfo hint = { 0, };
        addrinfo* results = nullptr;

        hint.ai_family = AF_INET6;

        if (!getaddrinfo(in_string, nullptr, &hint, &results)) {
            bool found = false;

            for (addrinfo* rp = results; rp != nullptr; rp = rp->ai_next) {
                if (rp->ai_family == AF_INET6) {
                    sr.ipv6_addr = *((sockaddr_in6*)rp->ai_addr);
                    found = true;
                    break;
                }
            }

            freeaddrinfo(results);

            if (found) {
                sockaddr_to_ipv6(sr, out);
                return true;
            }
        }

        return false;
    }

    NHTTP_API bool resolve_all(std::vector<ipv4_addr>& out, const char* in_string) {
        sockaddr_sr sr = { 0, };
        ipv4_addr tmp;

        sr.ipv4_addr.sin_family = AF_INET;

        if (inet_pton(AF_INET, in_string, &sr.ipv4_addr.sin_addr)) {
            sockaddr_to_ipv4(sr, tmp);
            out.push_back(tmp);
            return true;
        }

        addrinfo hint = { 0, };
        addrinfo* results = nullptr;

        hint.ai_family = AF_INET;

        if (!getaddrinfo(in_string, nullptr, &hint, &results)) {
            bool found = false;
            size_t n = out.size();

            for (addrinfo* rp = results; rp != nullptr; rp = rp->ai_next) {
                if (rp->ai_family == AF_INET) {
                    sr.ipv4_addr = *((sockaddr_in*)rp->ai_addr);
                    sockaddr_to_ipv4(sr, tmp);
                    out.push_back(tmp);
                }
            }

            freeaddrinfo(results);
            return n != out.size();
        }

        return false;
    }

    NHTTP_API bool resolve_all(std::vector<ipv6_addr>& out, const char* in_string) {
        sockaddr_sr sr = { 0, };
        ipv6_addr tmp;

        sr.ipv6_addr.sin6_family = AF_INET6;

        if (inet_pton(AF_INET6, in_string, &sr.ipv6_addr.sin6_addr)) {
            sockaddr_to_ipv6(sr, tmp);
            out.push_back(tmp);
            return true;
        }

        addrinfo hint = { 0, };
        addrinfo* results = nullptr;

        hint.ai_family = AF_INET6;

        if (!getaddrinfo(in_string, nullptr, &hint, &results)) {
            bool found = false;
            size_t n = out.size();

            for (addrinfo* rp = results; rp != nullptr; rp = rp->ai_next) {
                if (rp->ai_family == AF_INET6) {
                    sr.ipv6_addr = *((sockaddr_in6*)rp->ai_addr);
                    sockaddr_to_ipv6(sr, tmp);
                    out.push_back(tmp);
                }
            }

            freeaddrinfo(results);
            return n != out.size();
        }

        return false;
    }

    socket_fd_t socket_raw_t::create(bool v4, bool tcp) {
#if NHTTP_OS_WINDOWS
        static WSAInit _init;
#else
        static bool _initialized = false;
#endif

#if !NHTTP_OS_WINDOWS
        if (!_initialized) {
            // Don't signal on socket write errors.
            ::signal(SIGPIPE, SIG_IGN);
            _initialized = true;
        }
#endif

        return ::socket(v4 ? AF_INET : AF_INET6,
            tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    }

    int32_t socket_raw_t::get_last_error() {
#if NHTTP_OS_WINDOWS
        DWORD error = ::WSAGetLastError();

        switch(error) {
        case WSAEWOULDBLOCK:
            error = EWOULDBLOCK;
            break;

        case WSATRY_AGAIN:
            error = EAGAIN;
            break;

        case WSA_IO_INCOMPLETE:
        case WSA_IO_PENDING:
            error = EWOULDBLOCK;
            break;
        }

        return error;
#else
        return errno;
#endif
    }

    std::string socket_raw_t::error_str() const { 
        return strerror(err);
    }

    socket_raw_t socket_raw_t::clone() {
        socket_fd_t new_fd = INVALID_SOCKET_FD;

        if (fd != INVALID_SOCKET_FD) {
#if NHTTP_OS_WINDOWS
            WSAPROTOCOL_INFOW protInfo;
            if (::WSADuplicateSocketW(fd, ::GetCurrentProcessId(), &protInfo) == 0)
                new_fd = ::WSASocketW(AF_INET, SOCK_STREAM, 0, &protInfo, 0, WSA_FLAG_OVERLAPPED);
#else
            fd = ::dup(fd);
#endif
            if (fd != INVALID_SOCKET_FD)
                return socket_raw_t(new_fd, 0);

            return socket_raw_t(new_fd, ENOTSUP);
        }

        return socket_raw_t(new_fd, ENOTSOCK);
    }

    bool socket_raw_t::check_return(int32_t ret_val) const {
        if (!ret_val) {
            return true;
        }

        err = get_last_error();
        return false;
    }

    bool socket_raw_t::bind(void* data, size_t size) {
        sockaddr_sr sr = { 0, };

        if (fd != INVALID_SOCKET_FD) {
            switch (size) {
                case sizeof(ipv4_addr):
                    ipv4_to_sockaddr(*((ipv4_addr*)data), sr);
                    return check_return(::bind(fd, (sockaddr*) &sr.ipv4_addr, sizeof(sockaddr_in)));

                case sizeof(ipv6_addr):
                    ipv6_to_sockaddr(*((ipv6_addr*)data), sr);
                    return check_return(::bind(fd, (sockaddr*)&sr.ipv6_addr, sizeof(sockaddr_in6)));
            }

            err = ENOTSUP;
            return false;
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::listen(int32_t backlog) {
        if (fd != INVALID_SOCKET_FD) {
            return check_return(::listen(fd, backlog));
        }

        err = ENOTSOCK;
        return false;
    }

    socket_raw_t socket_raw_t::accept() {
        if (fd != INVALID_SOCKET_FD) {
            sockaddr_storage sa;
            socklen_t len = sizeof(sa);

            socket_fd_t s = ::accept(fd, (sockaddr*) &sa, &len);

            if (s != INVALID_SOCKET_FD)
                return socket_raw_t(s);

            return socket_raw_t(s, get_last_error());
        }

        return socket_raw_t(INVALID_SOCKET_FD, ENOTSOCK);
    }

    bool socket_raw_t::get_local_address(void* out, size_t size) {
        sockaddr_storage sa;
        socklen_t len = sizeof(sa);

        if (fd != INVALID_SOCKET_FD) {
            bool state = check_return(::getsockname(fd, (sockaddr*) &sa, &len));

            if (state) {
                switch (sa.ss_family) {
                    case AF_INET:
                        if (sizeof(ipv4_addr) == size) {
                            sockaddr_sr& sr = *((sockaddr_sr*)&sa);
                            sockaddr_to_ipv4(sr, *((ipv4_addr*)out));
                            return true;
                        }

                        break;

                    case AF_INET6:
                        if (sizeof(ipv6_addr) == size) {
                            sockaddr_sr& sr = *((sockaddr_sr*)&sa);
                            sockaddr_to_ipv6(sr, *((ipv6_addr*)out));
                            return true;
                        }
                        
                        break;
                }

                err = EINVAL;
                return false;
            }

            return false;
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::get_remote_address(void* out, size_t size) {
        sockaddr_storage sa;
        socklen_t len = sizeof(sa);

        if (fd != INVALID_SOCKET_FD) {
            bool state = check_return(::getpeername(fd, (sockaddr*)&sa, &len));

            if (state) {
                switch (sa.ss_family) {
                case AF_INET:
                    if (sizeof(ipv4_addr) == size) {
                        sockaddr_sr& sr = *((sockaddr_sr*)&sa);
                        sockaddr_to_ipv4(sr, *((ipv4_addr*)out));
                        return true;
                    }

                    break;

                case AF_INET6:
                    if (sizeof(ipv6_addr) == size) {
                        sockaddr_sr& sr = *((sockaddr_sr*)&sa);
                        sockaddr_to_ipv6(sr, *((ipv6_addr*)out));
                        return true;
                    }

                    break;
                }

                err = EINVAL;
                return false;
            }

            return false;
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::get_option(int level, int optname, void* optval, socklen_t* optlen) const {

        if (fd != INVALID_SOCKET_FD) {
#if NHTTP_OS_WINDOWS
            if (optval && optlen) {
                int len = static_cast<int>(*optlen);
                if (check_return(::getsockopt(fd, level, optname, (char*)optval, &len))) {
                    *optlen = static_cast<socklen_t>(len);
                    return true;
                }
            }
            
            err = ENOPROTOOPT;
            return false;
#else
            return check_return(::getsockopt(fd, level, optname, optval, optlen));
#endif
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::set_option(int level, int optname, const void* optval, socklen_t optlen) const {
        if (fd != INVALID_SOCKET_FD) {
#if NHTTP_OS_WINDOWS
            return check_return(::setsockopt(fd, level, optname, (const char* )optval, optlen));
#else
            return check_return(::setsockopt(fd, level, optname, optval, optlen));
#endif
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::set_non_blocking(bool on) {
        if (fd != INVALID_SOCKET_FD) {
#if NHTTP_OS_WINDOWS
            unsigned long mode = on ? 1 : 0;
            return check_return(::ioctlsocket(fd, FIONBIO, &mode));
#else
            int flags = ::fcntl(fd, F_GETFL, 0);

            if (flags == -1) {
                err = get_last_error();
                return false;
            }

            flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
            if (::fcntl(fd, F_SETFL, flags) == -1) {
                err = get_last_error();
                return false;
            }

            return true;
#endif
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::set_naggle_enabled(bool on) {
        int val = on ? 1 : 0;
        return set_option(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(int));
    }

    bool socket_raw_t::set_reuse_address(bool allow) {
        int val = allow ? 1 : 0;
        return set_option(SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    }

    bool socket_raw_t::set_read_timeout(int32_t millisec) {
        timeval tv;

        tv.tv_sec = (millisec / 1000);
        tv.tv_usec = (millisec % 1000) * 1000;

        return set_option(SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    bool socket_raw_t::set_write_timeout(int32_t millisec) {
        timeval tv;

        tv.tv_sec = (millisec / 1000);
        tv.tv_usec = (millisec % 1000) * 1000;

        return set_option(SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    bool socket_raw_t::set_keepalive(uint32_t quiets, uint32_t probes, uint32_t count) {
        if (fd != INVALID_SOCKET_FD) {
#if NHTTP_OS_WINDOWS
            tcp_keepalive ka = { 0, };
            DWORD ret;

            ka.onoff = quiets > 0 && probes > 0;
            if (ka.onoff) {
                ka.keepalivetime = quiets < 1000 ? 1000 : quiets;
                ka.keepaliveinterval = probes < 1000 ? 1000 : probes;
            }
            
            if (WSAIoctl(fd, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), 0, 0, &ret, 0, 0)) {
                err = get_last_error();
                return false;
            }
#else
            int enable = quiets > 0 && probes > 0 ? 1 : 0;
            
            quiets = quiets < 1000 ? 1 : (quiets / 1000);
            probes = probes < 1000 ? 1 : (probes / 1000);

            if (!check_return(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(int))))
                return false;

            if (enable) {
                int idle = quiets;
                if (!check_return(setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(int))))
                    return set_keepalive(0, 0);

                int interval = probes;
                if (!check_return(setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(int))))
                    return set_keepalive(0, 0);

                int maxpkt = count <= 1 ? 1 : count;
                if (!check_return(setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int))))
                    return set_keepalive(0, 0);
            }
#endif
            return true;
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::set_linger(int32_t timeout) {
        linger _linger = { 0, };

        _linger.l_onoff = timeout >= 0 ? 1 : 0;
        _linger.l_linger = timeout == 0 ? 0 : (timeout < 1000 ? 1 : timeout / 1000);

        return set_option(SOL_SOCKET, SO_LINGER, &_linger, sizeof(_linger));
    }

    ssize_t socket_raw_t::read(void* buf, size_t n) {
        if (n > 0x7ffffffful) n = 0x7ffffffful;
        if (fd != INVALID_SOCKET_FD) {
            ssize_t reads = ::recv(fd, (char*) buf, int32_t(n), 0);

            if (reads < 0) {
                err = get_last_error();
            }

            return reads;
        }

        err = ENOTSOCK;
        return -1;
    }

    ssize_t socket_raw_t::read_n(void* buf, size_t n) {
        if (fd != INVALID_SOCKET_FD) {
            uint8_t* bytes = (uint8_t*)buf;
            ssize_t retval = 0;

            while (n) {
                ssize_t block = read(bytes, n);

                if (block < 0) {
                    if (err == EINTR)
                        continue;

                    retval = -1;
                    break;
                }

                if (!block) {
                    break;
                }

                bytes += block;
                retval += block;
                n -= block;
            }

            return retval;
        }

        err = ENOTSOCK;
        return -1;
    }

    ssize_t socket_raw_t::write(const void* buf, size_t n) {
        if (n > 0x7ffffffful) n = 0x7ffffffful;
        if (fd != INVALID_SOCKET_FD) {
            ssize_t written = ::send(fd, (const char*)buf, int32_t(n), 0);

            if (written < 0) {
                err = get_last_error();
            }

            return written;
        }

        err = ENOTSOCK;
        return -1;
    }

    ssize_t socket_raw_t::write_n(const void* buf, size_t n) {
        if (fd != INVALID_SOCKET_FD) {
            uint8_t* bytes = (uint8_t*)buf;
            ssize_t retval = 0;

            while (n) {
                ssize_t block = write(bytes, n);

                if (block < 0) {
                    if (err == EINTR)
                        continue;

                    retval = -1;
                    break;
                }

                if (!block) {
                    break;
                }

                bytes += block;
                retval += block;
                n -= block;
            }

            return retval;
        }

        err = ENOTSOCK;
        return -1;
    }

    bool socket_raw_t::shutdown(int how) {
        if (fd != INVALID_SOCKET_FD) {
            return check_return(::shutdown(fd, how));
        }

        err = ENOTSOCK;
        return false;
    }

    bool socket_raw_t::close() {
        if (fd != INVALID_SOCKET_FD) {
#if NHTTP_OS_WINDOWS
            ::closesocket(fd);
#else
            ::close(fd);
#endif

            return true;
        }

        err = ENOTSOCK;
        return false;
    }

}
}